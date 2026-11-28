#pragma once

#include <cstddef>
#include <cstdint>

#include "brick/Status.h"

namespace brick::interfaces::system
{

/// A new program for a device, taken piece by piece: the device's own (an
/// ESP32 writes it into its other program slot) or its co-processor's
/// (RemoteNetworkHost::update() passes it over the link). The program in use
/// stays until finish() has said ok and the device restarts.
class IFirmwareUpdate
{
public:
    virtual ~IFirmwareUpdate() = default;

    /// Starts an update with an image of size bytes. busy: one runs already;
    /// no_memory: the image does not fit; not_supported: the device takes none.
    virtual Status begin(std::uint32_t size) = 0;
    /// The next bytes of the image. written says how many were taken: maybe
    /// none (come again), from a device's own flash all. An error ends the
    /// update.
    virtual Status write(const std::uint8_t* data, std::size_t size, std::size_t& written) = 0;
    /// After the last byte: busy while the image is written and checked, then
    /// ok (it starts at the device's next restart) or what is wrong with it.
    /// An error ends the update.
    virtual Status finish() = 0;
    /// Gives a running update up; nothing to do without one.
    virtual void abort() = 0;
    /// Restarts the device: into the new program after finish() said ok. A
    /// device restarting itself does not return.
    virtual Status restart() = 0;
    /// What names the program in use (version and build time); empty when not
    /// known (yet).
    virtual const char* description() const = 0;
};

}  // namespace brick::interfaces::system
