#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "brick/Status.h"
#include "brick/interfaces/communication/ISerialPort.h"

namespace brick::platform::pc
{

/// Serial port for the simulator and tests. Incoming bytes come from inject()
/// or, when given, from a file read at begin() (a recorded host session);
/// written bytes are kept for output().
class PcSerialPort final : public brick::interfaces::communication::ISerialPort
{
public:
    explicit PcSerialPort(std::string input_file = {}) : input_file_(std::move(input_file)) {}

    Status begin() override
    {
        if (input_file_.empty())
            return Status::ok;
        std::ifstream file(input_file_, std::ios::binary);
        if (!file)
            return Status::not_found;
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        for (const char byte : bytes)
            input_.push_back(static_cast<std::uint8_t>(byte));
        return Status::ok;
    }

    std::size_t read(std::uint8_t* buffer, std::size_t capacity) override
    {
        std::size_t count = 0;
        while (count < capacity && !input_.empty())
        {
            buffer[count++] = input_.front();
            input_.pop_front();
        }
        return count;
    }

    std::size_t write(const std::uint8_t* data, std::size_t length) override
    {
        output_.insert(output_.end(), data, data + length);
        return length;
    }

    void inject(const std::uint8_t* data, std::size_t length) { input_.insert(input_.end(), data, data + length); }
    const std::vector<std::uint8_t>& output() const { return output_; }

private:
    std::string               input_file_;
    std::deque<std::uint8_t>  input_;
    std::vector<std::uint8_t> output_;
};

}  // namespace brick::platform::pc
