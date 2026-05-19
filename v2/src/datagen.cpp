#include "caidj/datagen.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace caidj
{

    DataGen::DataGen(uint64_t seed, double zipf_alpha, int64_t domain_size)
        : seed_(seed), domain_size_(domain_size), zipf_(zipf_alpha, domain_size, seed), rng_(seed ^ 0x9E3779B97F4A7C15ULL) {}

    Tuple DataGen::make_tuple(TID tid)
    {
        std::uniform_int_distribution<int64_t> payload(0, 1'000'000);
        return Tuple{tid, zipf_.next(), payload(rng_), payload(rng_)};
    }

    Relation DataGen::generate_R(int64_t num_tuples)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Relation relation;
        relation.reserve(static_cast<size_t>(std::max<int64_t>(0, num_tuples)));
        for (int64_t i = 0; i < num_tuples; ++i)
        {
            relation.push_back(make_tuple(i));
        }
        return relation;
    }

    Relation DataGen::generate_S(int64_t num_tuples)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Relation relation;
        relation.reserve(static_cast<size_t>(std::max<int64_t>(0, num_tuples)));
        constexpr TID offset = 1'000'000'000LL;
        for (int64_t i = 0; i < num_tuples; ++i)
        {
            relation.push_back(make_tuple(offset + i));
        }
        return relation;
    }

    WriteOp DataGen::generate_write_op(OpType op)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uniform_int_distribution<TID> tid_dist(0, (1LL << 31) - 1);
        return WriteOp{op, zipf_.next(), tid_dist(rng_)};
    }

    void DataGen::save_csv(const Relation &rel, const std::string &path)
    {
        const std::filesystem::path fs_path(path);
        if (fs_path.has_parent_path())
        {
            std::filesystem::create_directories(fs_path.parent_path());
        }
        std::ofstream out(path);
        if (!out)
        {
            throw std::runtime_error("could not write CSV: " + path);
        }
        out << "tid,key,val1,val2\n";
        for (const auto &t : rel)
        {
            out << t.tid << ',' << t.key << ',' << t.val1 << ',' << t.val2 << '\n';
        }
    }

    Relation DataGen::load_csv(const std::string &path)
    {
        std::ifstream in(path);
        if (!in)
        {
            throw std::runtime_error("could not read CSV: " + path);
        }
        Relation rel;
        std::string line;
        bool first = true;
        while (std::getline(in, line))
        {
            if (first)
            {
                first = false;
                if (line.find("tid") != std::string::npos)
                {
                    continue;
                }
            }
            if (line.empty())
            {
                continue;
            }
            std::stringstream ss(line);
            std::string field;
            Tuple t;
            std::getline(ss, field, ',');
            t.tid = std::stoll(field);
            std::getline(ss, field, ',');
            t.key = std::stoll(field);
            std::getline(ss, field, ',');
            t.val1 = std::stoll(field);
            std::getline(ss, field, ',');
            t.val2 = std::stoll(field);
            rel.push_back(t);
        }
        return rel;
    }

} // namespace caidj