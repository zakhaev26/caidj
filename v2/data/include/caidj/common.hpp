#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace caidj {

using Key = int64_t;
using TID = int64_t;
using TxnID = uint64_t;
using NodeID = uint32_t;

constexpr TxnID INF_TS = std::numeric_limits<TxnID>::max();
constexpr Key NULL_KEY = std::numeric_limits<Key>::min();
constexpr TID NULL_TID = -1;

enum class OpType : uint8_t { INSERT, DELETE };

struct WriteOp {
    OpType op{OpType::INSERT};
    Key key{NULL_KEY};
    TID tid{NULL_TID};
};

struct Tuple {
    TID tid{};
    Key key{};
    int64_t val1{};
    int64_t val2{};

    friend bool operator==(const Tuple& a, const Tuple& b) noexcept {
        return a.tid == b.tid && a.key == b.key && a.val1 == b.val1 && a.val2 == b.val2;
    }
};

using Relation = std::vector<Tuple>;

struct ProbeResult {
    Key key{NULL_KEY};
    std::vector<TID> matching_tids;
    bool was_fast_path{false};
};

#define CAIDJ_NONCOPYABLE(T)          \
    T(const T&) = delete;             \
    T& operator=(const T&) = delete;  \
    T(T&&) = delete;                  \
    T& operator=(T&&) = delete

enum class Protocol : uint8_t { NHJ, ECHI, MPI_MVCC, BF_CSI };

inline std::string normalize_protocol_name(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (ch == '-' || ch == '_' || std::isspace(ch) != 0) {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

inline Protocol protocol_from_string(const std::string& s) {
    const auto n = normalize_protocol_name(s);
    if (n == "nhj") {
        return Protocol::NHJ;
    }
    if (n == "echi") {
        return Protocol::ECHI;
    }
    if (n == "mpimvcc") {
        return Protocol::MPI_MVCC;
    }
    if (n == "bfcsi") {
        return Protocol::BF_CSI;
    }
    throw std::invalid_argument("unknown protocol: " + s);
}

inline std::string protocol_to_string(Protocol p) {
    switch (p) {
    case Protocol::NHJ:
        return "NHJ";
    case Protocol::ECHI:
        return "ECHI";
    case Protocol::MPI_MVCC:
        return "MPI-MVCC";
    case Protocol::BF_CSI:
        return "BF-CSI";
    }
    return "UNKNOWN";
}

inline std::string protocol_to_cli_string(Protocol p) {
    switch (p) {
    case Protocol::NHJ:
        return "nhj";
    case Protocol::ECHI:
        return "echi";
    case Protocol::MPI_MVCC:
        return "mpimvcc";
    case Protocol::BF_CSI:
        return "bfcsi";
    }
    return "unknown";
}

enum class CaidjError {
    OK = 0,
    CONFIG_INVALID,
    FILE_NOT_FOUND,
    FILE_WRITE_ERROR,
    THREAD_SPAWN_ERROR,
};

struct CaidjResult {
    CaidjError code{CaidjError::OK};
    std::string message;
    [[nodiscard]] bool ok() const noexcept { return code == CaidjError::OK; }
};

} // namespace caidj