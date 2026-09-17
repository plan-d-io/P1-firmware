#include "P1DSMR.h"

namespace p1dsmr {

Reader::Reader() : acc_(std::span<std::uint8_t>(buf_, kBufferSize), true)
{
    new (data_store_) Data();
}

Reader::~Reader()
{
    data_mut().~Data();
}

void Reader::reset_data()
{
    data_mut().~Data();
    new (data_store_) Data();
}

Data& Reader::data_mut()
{
    return *std::launder(reinterpret_cast<Data*>(data_store_));
}

const Data& Reader::data() const
{
    return *std::launder(reinterpret_cast<const Data*>(data_store_));
}

bool Reader::parse_telegram(const dsmr_parser::DsmrUnencryptedTelegram& telegram)
{
    reset_data();
    return dsmr_parser::DsmrParser::parse(data_mut(), telegram, false);
}

Reader::Status Reader::feed(std::uint8_t byte)
{
    auto parsed = acc_.process_byte(byte);

    if (byte == '/') {
        crc_nibbles_left_ = 0;
    }

    if (parsed) {
        crc_nibbles_left_ = 0;
        if (!parse_telegram(*parsed)) {
            has_ = false;
            raw_.clear();
            return Status::Rejected;
        }
        raw_.assign(parsed->full_content());
        has_ = true;
        return Status::Ok;
    }

    if (byte == '!') {
        crc_nibbles_left_ = 4;
        return Status::NeedMore;
    }

    if (crc_nibbles_left_ > 0) {
        --crc_nibbles_left_;
        if (crc_nibbles_left_ == 0) {
            has_ = false;
            raw_.clear();
            return Status::Rejected;
        }
    }

    return Status::NeedMore;
}

FeedStats Reader::feed(std::span<const std::uint8_t> bytes)
{
    return feed(bytes, [](const Data&) {});
}

}  // namespace p1dsmr
