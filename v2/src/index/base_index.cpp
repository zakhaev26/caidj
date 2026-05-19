#include "caidj/index/base_index.hpp"

#include "caidj/config.hpp"
#include "caidj/index/bfcsi_index.hpp"
#include "caidj/index/echi_index.hpp"
#include "caidj/index/mpimvcc_index.hpp"
#include "caidj/index/nhj_index.hpp"

#include <stdexcept>

namespace caidj::index
{

    std::unique_ptr<BaseIndex> make_index(Protocol p, const Config &cfg)
    {
        switch (p)
        {
        case Protocol::NHJ:
            return std::make_unique<NHJIndex>();
        case Protocol::ECHI:
            return std::make_unique<ECHIIndex>(cfg);
        case Protocol::MPI_MVCC:
            return std::make_unique<MPIMVCCIndex>(cfg);
        case Protocol::BF_CSI:
            return std::make_unique<BFCSIIndex>(cfg);
        }
        throw std::invalid_argument("unsupported protocol");
    }

} // namespace caidj::index