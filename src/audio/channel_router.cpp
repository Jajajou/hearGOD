#include "hearGOD/channel_router.h"

namespace hearGOD {

ChannelRouter::ChannelRouter()
{
    mapping_.fill(Channel::INVALID);

    // Default 9.1.6 layout (Apple Music Atmos / Dolby Atmos 16ch, ITU-R BS.2094)
    mapping_[0]  = Channel::FL;
    mapping_[1]  = Channel::FR;
    mapping_[2]  = Channel::FC;
    mapping_[3]  = Channel::LFE;
    mapping_[4]  = Channel::BL;
    mapping_[5]  = Channel::BR;
    mapping_[6]  = Channel::SL;
    mapping_[7]  = Channel::SR;
    mapping_[8]  = Channel::TFL;
    mapping_[9]  = Channel::TFR;
    mapping_[10] = Channel::TBL;
    mapping_[11] = Channel::TBR;
    mapping_[12] = Channel::LSS;
    mapping_[13] = Channel::RSS;
    mapping_[14] = Channel::TSL;
    mapping_[15] = Channel::TSR;
}

void ChannelRouter::setMapping(int inputIdx, Channel ch)
{
    if (inputIdx >= 0 && inputIdx < MAX_CHANNELS)
        mapping_[inputIdx] = ch;
}

Channel ChannelRouter::channelFor(int inputIdx) const
{
    if (inputIdx < 0 || inputIdx >= MAX_CHANNELS)
        return Channel::INVALID;
    return mapping_[inputIdx];
}

int ChannelRouter::numMapped() const
{
    int count = 0;
    for (auto ch : mapping_)
        if (ch != Channel::INVALID) ++count;
    return count;
}

} // namespace hearGOD
