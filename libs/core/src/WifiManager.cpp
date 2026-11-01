#include "brick/core/network/WifiManager.h"

#include "brick/core/network/WifiConfigCheck.h"

namespace brick::core::network
{

using interfaces::network::LinkState;
using interfaces::network::WifiFailure;

namespace
{
    constexpr char kTag[] = "brick_wifi_manager";
}  // namespace

WifiManager::WifiManager(interfaces::network::IWifiStation& station, interfaces::time::ITimeProvider& time,
                         interfaces::logging::ILogger& logger, interfaces::network::IWifiAccessPoint* access_point)
    : station_(station), access_point_(access_point), logger_(logger), timer_(time)
{
}

Status WifiManager::validate_(const WifiManagerConfig& config) const
{
    if (config.network_count > 0 && config.networks == nullptr)
        return Status::invalid_argument;
    for (std::size_t index = 0; index < config.network_count; ++index)
        if (const Status status = check_credentials(config.networks[index]); !is_ok(status))
            return status;
    if (config.fallback_access_point == nullptr)
        return config.network_count > 0 ? Status::ok : Status::invalid_argument;
    if (access_point_ == nullptr)
        return Status::invalid_argument;
    return check_access_point(*config.fallback_access_point);
}

Status WifiManager::start(const WifiManagerConfig& config)
{
    if (const Status status = validate_(config); !is_ok(status))
        return status;
    stop();
    config_          = config;
    failed_attempts_ = 0;
    last_failure_    = WifiFailure::none;
    last_failed_     = kNoWifiNetwork;
    enter_scanning_();
    return Status::ok;
}

void WifiManager::stop()
{
    if (state_ == WifiManagerState::stopped)
        return;
    if (const Status status = station_.disconnect(); !is_ok(status))
        logger_.error(kTag, "Leaving the network failed: %s", to_string(status));
    close_fallback_();
    state_          = WifiManagerState::stopped;
    current_        = kNoWifiNetwork;
    scan_requested_ = false;
}

void WifiManager::update()
{
    switch (state_)
    {
        case WifiManagerState::stopped:
            break;
        case WifiManagerState::scanning:
            update_scanning_();
            break;
        case WifiManagerState::connecting:
            update_connecting_();
            break;
        case WifiManagerState::connected:
            update_connected_();
            break;
        case WifiManagerState::waiting:
            update_waiting_();
            break;
    }
}

Status WifiManager::request_scan()
{
    if (state_ == WifiManagerState::stopped)
        return Status::not_ready;
    if (scan_requested_ || (state_ != WifiManagerState::connected && state_ != WifiManagerState::waiting))
        return Status::busy;
    const Status status = station_.start_scan();
    scan_requested_     = is_ok(status);
    return status;
}

const interfaces::network::WifiCredentials* WifiManager::current_network() const
{
    return current_ == kNoWifiNetwork ? nullptr : &config_.networks[current_];
}

void WifiManager::enter_scanning_()
{
    state_          = WifiManagerState::scanning;
    scan_requested_ = false;
    timer_.start(config_.scan_timeout_ms);
    // Status::busy: a requested scan is still running; its result is used.
    if (const Status status = station_.start_scan(); !is_ok(status) && status != Status::busy)
    {
        logger_.error(kTag, "Starting a scan failed: %s", to_string(status));
        fail_(WifiFailure::other);
    }
}

void WifiManager::update_scanning_()
{
    const Status status = store_scan_();
    if (status == Status::busy)
    {
        if (timer_.expired())
            fail_(WifiFailure::timeout);
        return;
    }
    if (!is_ok(status))
    {
        logger_.error(kTag, "Scan failed: %s", to_string(status));
        fail_(WifiFailure::other);
        return;
    }
    const std::size_t index =
        choose_wifi_network(config_.networks, config_.network_count, networks_.data(), network_count_, last_failed_);
    if (index == kNoWifiNetwork)
    {
        fail_(WifiFailure::network_not_found);
        return;
    }
    enter_connecting_(index);
}

void WifiManager::enter_connecting_(std::size_t index)
{
    current_ = index;
    state_   = WifiManagerState::connecting;
    timer_.start(config_.connect_timeout_ms);
    logger_.info(kTag, "Joining %s", config_.networks[index].ssid);
    if (const Status status = station_.connect(config_.networks[index]); !is_ok(status))
    {
        logger_.error(kTag, "Joining %s failed: %s", config_.networks[index].ssid, to_string(status));
        fail_(WifiFailure::other);
    }
}

void WifiManager::update_connecting_()
{
    switch (station_.link_state())
    {
        case LinkState::up:
            on_connected_();
            return;
        case LinkState::down:
            // Without a failure the station has not picked up connect() yet.
            if (const WifiFailure failure = station_.failure(); failure != WifiFailure::none)
            {
                fail_(failure);
                return;
            }
            break;
        case LinkState::connecting:
            break;
    }
    if (!timer_.expired())
        return;
    if (const Status status = station_.disconnect(); !is_ok(status))
        logger_.error(kTag, "Leaving the network failed: %s", to_string(status));
    fail_(WifiFailure::timeout);
}

void WifiManager::on_connected_()
{
    state_           = WifiManagerState::connected;
    failed_attempts_ = 0;
    last_failure_    = WifiFailure::none;
    last_failed_     = kNoWifiNetwork;
    logger_.info(kTag, "Connected to %s", config_.networks[current_].ssid);
    close_fallback_();
}

void WifiManager::update_connected_()
{
    switch (station_.link_state())
    {
        case LinkState::up:
            poll_requested_scan_();
            return;
        case LinkState::connecting:
            // Still joined, the address is being renewed: give it the usual time.
            state_ = WifiManagerState::connecting;
            timer_.start(config_.connect_timeout_ms);
            return;
        case LinkState::down:
            break;
    }
    const WifiFailure failure = station_.failure();
    logger_.warning(kTag, "Lost %s: %s", config_.networks[current_].ssid, to_string(failure));
    fail_(failure == WifiFailure::none ? WifiFailure::connection_lost : failure);
}

void WifiManager::update_waiting_()
{
    poll_requested_scan_();
    if (timer_.expired())
        enter_scanning_();
}

void WifiManager::fail_(WifiFailure failure)
{
    last_failure_ = failure;
    if (current_ != kNoWifiNetwork)
        last_failed_ = current_;
    current_ = kNoWifiNetwork;
    if (failed_attempts_ < UINT16_MAX)
        ++failed_attempts_;
    const std::uint32_t delay_ms = retry_delay_();
    logger_.warning(kTag, "Attempt %u failed: %s; next in %lu ms", static_cast<unsigned>(failed_attempts_),
                    to_string(failure), static_cast<unsigned long>(delay_ms));
    if (config_.network_count == 0 || failed_attempts_ >= config_.attempts_before_fallback)
        open_fallback_();
    state_ = WifiManagerState::waiting;
    timer_.start(delay_ms);
}

Status WifiManager::store_scan_()
{
    std::size_t  count  = 0;
    const Status status = station_.scan_results(networks_.data(), networks_.size(), count);
    if (is_ok(status))
    {
        network_count_ = count;
        ++scan_count_;
    }
    return status;
}

void WifiManager::poll_requested_scan_()
{
    if (!scan_requested_)
        return;
    const Status status = store_scan_();
    if (status == Status::busy)
        return;
    scan_requested_ = false;
    if (!is_ok(status))
        logger_.error(kTag, "Scan failed: %s", to_string(status));
}

std::uint32_t WifiManager::retry_delay_() const
{
    std::uint32_t delay_ms = config_.retry_delay_ms;
    for (std::uint16_t attempt = 1; attempt < failed_attempts_ && delay_ms < config_.max_retry_delay_ms; ++attempt)
        delay_ms = delay_ms > config_.max_retry_delay_ms / 2 ? config_.max_retry_delay_ms : delay_ms * 2;
    return delay_ms < config_.max_retry_delay_ms ? delay_ms : config_.max_retry_delay_ms;
}

void WifiManager::open_fallback_()
{
    if (access_point_active_ || config_.fallback_access_point == nullptr || access_point_ == nullptr)
        return;
    if (const Status status = access_point_->start(*config_.fallback_access_point); !is_ok(status))
    {
        logger_.error(kTag, "Opening hotspot %s failed: %s", config_.fallback_access_point->ssid, to_string(status));
        return;
    }
    access_point_active_ = true;
    logger_.info(kTag, "Hotspot %s open", config_.fallback_access_point->ssid);
}

void WifiManager::close_fallback_()
{
    if (!access_point_active_)
        return;
    if (const Status status = access_point_->stop(); !is_ok(status))
        logger_.error(kTag, "Closing the hotspot failed: %s", to_string(status));
    access_point_active_ = false;
}

}  // namespace brick::core::network
