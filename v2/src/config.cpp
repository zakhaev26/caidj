#include "caidj/config.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace caidj
{
    namespace
    {

        std::string trim(std::string s)
        {
            auto not_space = [](unsigned char c)
            { return std::isspace(c) == 0; };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
            s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
            return s;
        }

        std::string strip_quotes(std::string s)
        {
            s = trim(std::move(s));
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
            {
                return s.substr(1, s.size() - 2);
            }
            return s;
        }

        std::string remove_comment(const std::string &line)
        {
            bool in_quote = false;
            char quote = '\0';
            for (size_t i = 0; i < line.size(); ++i)
            {
                const char c = line[i];
                if ((c == '"' || c == '\'') && (i == 0 || line[i - 1] != '\\'))
                {
                    if (!in_quote)
                    {
                        in_quote = true;
                        quote = c;
                    }
                    else if (quote == c)
                    {
                        in_quote = false;
                    }
                }
                if (c == '#' && !in_quote)
                {
                    return line.substr(0, i);
                }
            }
            return line;
        }

        bool parse_bool(std::string value)
        {
            value = normalize_protocol_name(value);
            if (value == "true" || value == "1" || value == "yes")
            {
                return true;
            }
            if (value == "false" || value == "0" || value == "no")
            {
                return false;
            }
            throw std::invalid_argument("invalid boolean value: " + value);
        }

        std::vector<std::string> parse_array_items(std::string value)
        {
            value = trim(std::move(value));
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
            {
                throw std::invalid_argument("expected array value, got: " + value);
            }
            value = value.substr(1, value.size() - 2);
            std::vector<std::string> out;
            std::string current;
            bool in_quote = false;
            char quote = '\0';
            for (size_t i = 0; i < value.size(); ++i)
            {
                const char c = value[i];
                if ((c == '"' || c == '\'') && (i == 0 || value[i - 1] != '\\'))
                {
                    if (!in_quote)
                    {
                        in_quote = true;
                        quote = c;
                    }
                    else if (quote == c)
                    {
                        in_quote = false;
                    }
                    current.push_back(c);
                }
                else if (c == ',' && !in_quote)
                {
                    out.push_back(trim(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!trim(current).empty())
            {
                out.push_back(trim(current));
            }
            return out;
        }

        std::vector<int> parse_int_array(const std::string &value)
        {
            std::vector<int> out;
            for (const auto &item : parse_array_items(value))
            {
                out.push_back(std::stoi(item));
            }
            return out;
        }

        std::vector<Protocol> parse_protocol_array(const std::string &value)
        {
            std::vector<Protocol> out;
            for (auto item : parse_array_items(value))
            {
                out.push_back(protocol_from_string(strip_quotes(std::move(item))));
            }
            return out;
        }

        std::string json_escape(const std::string &s)
        {
            std::ostringstream oss;
            for (char c : s)
            {
                switch (c)
                {
                case '"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                default:
                    oss << c;
                    break;
                }
            }
            return oss.str();
        }

        bool is_power_of_two(int n)
        {
            return n > 0 && (n & (n - 1)) == 0;
        }

        std::string require_value(int &i, int argc, char **argv, const std::string &option)
        {
            if (i + 1 >= argc)
            {
                throw std::invalid_argument("missing value for " + option);
            }
            return argv[++i];
        }

    } // namespace

    Config Config::from_toml(const std::string &path)
    {
        Config cfg;
        cfg.config_path = path;

        std::ifstream in(path);
        if (!in)
        {
            throw std::invalid_argument("could not open config file: " + path);
        }

        std::string section;
        std::string line;
        size_t line_no = 0;
        while (std::getline(in, line))
        {
            ++line_no;
            line = trim(remove_comment(line));
            if (line.empty())
            {
                continue;
            }
            if (line.front() == '[' && line.back() == ']')
            {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos)
            {
                throw std::invalid_argument("invalid TOML line " + std::to_string(line_no) + ": " + line);
            }
            const auto key = trim(line.substr(0, eq));
            const auto value = trim(line.substr(eq + 1));

            if (section == "data")
            {
                if (key == "seed")
                    cfg.seed = static_cast<uint64_t>(std::stoull(value));
                else if (key == "zipf_alpha")
                    cfg.zipf_alpha = std::stod(value);
                else if (key == "domain_size")
                    cfg.domain_size = std::stoll(value);
                else if (key == "r_size")
                    cfg.r_size = std::stoll(value);
                else if (key == "s_size")
                    cfg.s_size = std::stoll(value);
            }
            else if (section == "bench")
            {
                if (key == "num_runs")
                    cfg.num_runs = std::stoi(value);
                else if (key == "trial_duration_ms")
                    cfg.trial_duration_ms = static_cast<uint64_t>(std::stoull(value));
                else if (key == "num_probe_threads")
                    cfg.num_probe_threads = std::stoi(value);
                else if (key == "concurrency_levels")
                    cfg.concurrency_levels = parse_int_array(value);
                else if (key == "protocols")
                    cfg.protocols = parse_protocol_array(value);
            }
            else if (section == "output")
            {
                if (key == "dir")
                    cfg.output_dir = strip_quotes(value);
                else if (key == "write_csv")
                    cfg.write_csv = parse_bool(value);
                else if (key == "write_json")
                    cfg.write_json = parse_bool(value);
                else if (key == "log_level")
                    cfg.log_level = strip_quotes(value);
            }
            else if (section == "echi")
            {
                if (key == "delta_threshold")
                    cfg.echi_delta_threshold = static_cast<size_t>(std::stoull(value));
                else if (key == "epoch_interval_ms")
                    cfg.echi_epoch_interval_ms = static_cast<uint64_t>(std::stoull(value));
            }
            else if (section == "mpimvcc")
            {
                if (key == "gc_interval_ms")
                    cfg.mpimvcc_gc_interval_ms = static_cast<uint64_t>(std::stoull(value));
            }
            else if (section == "bfcsi")
            {
                if (key == "fpr")
                    cfg.bfcsi_fpr = std::stod(value);
                else if (key == "rebuild_threshold")
                    cfg.bfcsi_rebuild_threshold = std::stod(value);
                else if (key == "num_shards")
                    cfg.bfcsi_num_shards = std::stoi(value);
                else if (key == "fp_cache_capacity")
                    cfg.bfcsi_fp_cache_capacity = static_cast<size_t>(std::stoull(value));
            }
            else if (section == "txn")
            {
                if (key == "insert_fraction")
                    cfg.insert_fraction = std::stod(value);
            }
        }

        cfg.validate();
        return cfg;
    }

    Config Config::from_args(int argc, char **argv)
    {
        std::string config_path = "configs/default.toml";
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--config")
            {
                config_path = require_value(i, argc, argv, arg);
            }
        }

        Config cfg = Config::from_toml(config_path);

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--config")
            {
                (void)require_value(i, argc, argv, arg);
            }
            else if (arg == "--protocol")
            {
                const auto value = require_value(i, argc, argv, arg);
                if (normalize_protocol_name(value) == "all")
                {
                    cfg.protocols = {Protocol::NHJ, Protocol::ECHI, Protocol::MPI_MVCC, Protocol::BF_CSI};
                }
                else
                {
                    cfg.protocols = {protocol_from_string(value)};
                }
            }
            else if (arg == "--concurrency")
            {
                cfg.concurrency_levels = {std::stoi(require_value(i, argc, argv, arg))};
            }
            else if (arg == "--r-size")
            {
                cfg.r_size = std::stoll(require_value(i, argc, argv, arg));
            }
            else if (arg == "--s-size")
            {
                cfg.s_size = std::stoll(require_value(i, argc, argv, arg));
            }
            else if (arg == "--seed")
            {
                cfg.seed = static_cast<uint64_t>(std::stoull(require_value(i, argc, argv, arg)));
            }
            else if (arg == "--output")
            {
                cfg.output_dir = require_value(i, argc, argv, arg);
            }
            else if (arg == "--runs")
            {
                cfg.num_runs = std::stoi(require_value(i, argc, argv, arg));
            }
            else if (arg == "--duration")
            {
                cfg.trial_duration_ms = static_cast<uint64_t>(std::stoull(require_value(i, argc, argv, arg)));
            }
            else if (arg == "--probe-threads")
            {
                cfg.num_probe_threads = std::stoi(require_value(i, argc, argv, arg));
            }
            else if (arg == "--generate-only")
            {
                cfg.generate_only = true;
            }
            else if (arg == "--no-csv")
            {
                cfg.write_csv = false;
            }
            else if (arg == "--log-level")
            {
                cfg.log_level = require_value(i, argc, argv, arg);
            }
            else if (arg == "--help" || arg == "-h")
            {
                cfg.show_help = true;
            }
            else if (arg == "--version")
            {
                cfg.show_version = true;
            }
            else
            {
                throw std::invalid_argument("unknown option: " + arg);
            }
        }

        cfg.validate();
        return cfg;
    }

    void Config::validate() const
    {
        if (domain_size <= 0 || r_size < 0 || s_size < 0)
        {
            throw std::invalid_argument("data sizes must be non-negative and domain_size must be positive");
        }
        if (zipf_alpha < 0.0)
        {
            throw std::invalid_argument("zipf_alpha must be >= 0");
        }
        if (num_runs <= 0)
        {
            throw std::invalid_argument("num_runs must be positive");
        }
        if (num_probe_threads <= 0)
        {
            throw std::invalid_argument("num_probe_threads must be positive");
        }
        if (concurrency_levels.empty() || std::any_of(concurrency_levels.begin(), concurrency_levels.end(), [](int c)
                                                      { return c < 0; }))
        {
            throw std::invalid_argument("concurrency levels must be non-negative");
        }
        if (protocols.empty())
        {
            throw std::invalid_argument("at least one protocol must be selected");
        }
        if (echi_delta_threshold == 0)
        {
            throw std::invalid_argument("echi_delta_threshold must be positive");
        }
        if (bfcsi_fpr <= 0.0 || bfcsi_fpr >= 1.0)
        {
            throw std::invalid_argument("bfcsi_fpr must be in (0, 1)");
        }
        if (bfcsi_rebuild_threshold <= 0.0 || bfcsi_rebuild_threshold > 1.0)
        {
            throw std::invalid_argument("bfcsi_rebuild_threshold must be in (0, 1]");
        }
        if (!is_power_of_two(bfcsi_num_shards))
        {
            throw std::invalid_argument("bfcsi_num_shards must be a positive power of two");
        }
        if (bfcsi_fp_cache_capacity == 0)
        {
            throw std::invalid_argument("bfcsi_fp_cache_capacity must be positive");
        }
        if (insert_fraction < 0.0 || insert_fraction > 1.0)
        {
            throw std::invalid_argument("insert_fraction must be in [0, 1]");
        }
    }

    std::string config_to_json(const Config &cfg)
    {
        std::ostringstream oss;
        oss << std::boolalpha;
        oss << "{\n";
        oss << "  \"seed\": " << cfg.seed << ",\n";
        oss << "  \"zipf_alpha\": " << cfg.zipf_alpha << ",\n";
        oss << "  \"domain_size\": " << cfg.domain_size << ",\n";
        oss << "  \"r_size\": " << cfg.r_size << ",\n";
        oss << "  \"s_size\": " << cfg.s_size << ",\n";
        oss << "  \"num_runs\": " << cfg.num_runs << ",\n";
        oss << "  \"trial_duration_ms\": " << cfg.trial_duration_ms << ",\n";
        oss << "  \"num_probe_threads\": " << cfg.num_probe_threads << ",\n";
        oss << "  \"concurrency_levels\": [";
        for (size_t i = 0; i < cfg.concurrency_levels.size(); ++i)
        {
            if (i)
                oss << ", ";
            oss << cfg.concurrency_levels[i];
        }
        oss << "],\n  \"protocols\": [";
        for (size_t i = 0; i < cfg.protocols.size(); ++i)
        {
            if (i)
                oss << ", ";
            oss << '"' << protocol_to_cli_string(cfg.protocols[i]) << '"';
        }
        oss << "],\n";
        oss << "  \"output_dir\": \"" << json_escape(cfg.output_dir) << "\",\n";
        oss << "  \"write_csv\": " << cfg.write_csv << ",\n";
        oss << "  \"write_json\": " << cfg.write_json << ",\n";
        oss << "  \"log_level\": \"" << json_escape(cfg.log_level) << "\",\n";
        oss << "  \"echi_delta_threshold\": " << cfg.echi_delta_threshold << ",\n";
        oss << "  \"echi_epoch_interval_ms\": " << cfg.echi_epoch_interval_ms << ",\n";
        oss << "  \"mpimvcc_gc_interval_ms\": " << cfg.mpimvcc_gc_interval_ms << ",\n";
        oss << "  \"bfcsi_fpr\": " << cfg.bfcsi_fpr << ",\n";
        oss << "  \"bfcsi_rebuild_threshold\": " << cfg.bfcsi_rebuild_threshold << ",\n";
        oss << "  \"bfcsi_num_shards\": " << cfg.bfcsi_num_shards << ",\n";
        oss << "  \"bfcsi_fp_cache_capacity\": " << cfg.bfcsi_fp_cache_capacity << ",\n";
        oss << "  \"insert_fraction\": " << cfg.insert_fraction << "\n";
        oss << "}";
        return oss.str();
    }

} // namespace caidj