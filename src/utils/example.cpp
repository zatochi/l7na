#include <iostream>
#include <chrono>
#include <fstream>

#include <boost/tokenizer.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/memory_order.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/once.hpp>

#include "l7na/drives.h"
#include "l7na/configfile.h"
#include "l7na/logger.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;
namespace blog = boost::log;

struct Command {
    Drives::Axis axis;
    int32_t pos;
    int32_t vel;
    bool idle;

    Command()
        : axis(Drives::AZIMUTH_AXIS)
        , pos(0)
        , vel(0)
        , idle(false)
    {}

    void clear() {
        axis = Drives::AZIMUTH_AXIS;
        pos = 0;
        vel = 0;
        idle = false;
    }
};

bool parse_args(const std::string& cmd_str, Command& result) {

    typedef boost::tokenizer<boost::char_separator<char>> Tokenizer;
    boost::char_separator<char> sep(" ");

    Tokenizer tokens(cmd_str, sep);
    std::vector<std::string> cmd_vec;
    for (Tokenizer::iterator tok_iter = tokens.begin(); tok_iter != tokens.end(); ++tok_iter) {
        cmd_vec.push_back(*tok_iter);
    }

    if (cmd_vec[0] == "a") {
        result.axis = Drives::AZIMUTH_AXIS;
        if (cmd_vec.size() < 2) {
            std::cerr << "Invalid input for command 'a'" << std::endl;
            return false;
        }

        if (cmd_vec[1] == "v") {
            if (cmd_vec.size() < 3) {
                std::cerr << "Invalid input for command 'a v'" << std::endl;
                return false;
            }

            result.vel = std::atoi(cmd_vec[2].c_str());
        } else if (cmd_vec[1] == "p") {
            if (cmd_vec.size() < 3) {
                std::cerr << "Invalid input for command 'a p'" << std::endl;
                return false;
            }

            result.pos = std::atoi(cmd_vec[2].c_str());
        } else if (cmd_vec[1] == "i") {
            result.idle = true;
        }
    } else if (cmd_vec[0] == "e") {
        result.axis = Drives::ELEVATION_AXIS;
        if (cmd_vec.size() < 2) {
            std::cerr << "Invalid input for command 'e'" << std::endl;
            return false;
        }

        if (cmd_vec[1] == "v") {
            if (cmd_vec.size() < 3) {
                std::cerr << "Invalid input for command 'e v'" << std::endl;
                return false;
            }

            result.vel = std::atoi(cmd_vec[2].c_str());
        } else if (cmd_vec[1] == "p") {
            if (cmd_vec.size() < 3) {
                std::cerr << "Invalid input for command 'e p'" << std::endl;
                return false;
            }

            result.pos = std::atoi(cmd_vec[2].c_str());
        } else if (cmd_vec[1] == "i") {
            result.idle = true;
        }
    } else {
        std::cerr << "Invalid input" << std::endl;
        return false;
    }


    return true;
}

void print_status(const Drives::SystemStatus& status, std::ostream& os) {
    static bool header_printed = false;
    if (! header_printed) {
        os << "1.DateTime | 2.AxisA";
        os << "| 3.StateA | 4.StatusWordA | 5.ControlWordA | 6.ModeA | 7.CurPosA | 8.TgtPosA | 9.DmdPosA";
        os << "| 10.CurVelA | 11.TgtVelA | 12.DmdVelA | 13.CurTrqA | 14.CurTempA";
        os << "| 15.AxisE";
        os << "| 16.StateE |17.StatusWordE | 18.ControlWordE | 19.ModeE | 20.CurPosE | 21.TgtPosE | 22.DmdPosE";
        os << "| 23.CurVelE | 24.TgtVelE | 25.DmdVelE | 26.CurTrqE | 27.CurTempE";
        os << std::endl;
        header_printed = true;
    }
    os << boost::posix_time::to_simple_string(boost::posix_time::microsec_clock::local_time());
    for (int32_t axis = Drives::AXIS_MIN; axis < Drives::AXIS_COUNT; ++axis) {
         os << axis << "\t" << status.axes[axis].state << "\t" << std::hex << "0x" << status.axes[axis].statusword << "\t" << status.axes[axis].ctrlword
                  << std::dec << "\t" << status.axes[axis].mode
                  << "\t" << status.axes[axis].cur_pos  << "\t" << status.axes[axis].tgt_pos << "\t" << status.axes[axis].dmd_pos
                  << "\t" << status.axes[axis].cur_vel  << "\t" << status.axes[axis].tgt_vel << "\t" << status.axes[axis].dmd_vel
                  << "\t" << status.axes[axis].cur_torq << "\t" << status.axes[axis].cur_temperature;
    }
}

void print_status_cerr(const Drives::SystemStatus& status) {
    std::cerr << "System > state: " << status.state << std::endl;

    for (int32_t axis = Drives::AXIS_MIN; axis < Drives::AXIS_COUNT; ++axis) {
        std::cerr << "Axis " << axis << " > state: " << status.axes[axis].state << " statusword: " << std::hex << "0x" << status.axes[axis].statusword << " ctrlword: 0x" << status.axes[axis].ctrlword
                  << std::dec << " mode: " << status.axes[axis].mode
                  << " cur_pos: " << status.axes[axis].cur_pos  << " tgt_pos: " << status.axes[axis].tgt_pos << " dmd_pos: " << status.axes[axis].dmd_pos
                  << " cur_vel: " << status.axes[axis].cur_vel  << " tgt_vel: " << status.axes[axis].tgt_vel << " dmd_vel: " << status.axes[axis].dmd_vel
                  << " cur_trq: " << status.axes[axis].cur_torq << " cur_tmp: " << status.axes[axis].cur_temperature << std::endl;
    }
}

void print_info(const Drives::SystemInfo& info) {
    for (int32_t axis = Drives::AXIS_MIN; axis < Drives::AXIS_COUNT; ++axis) {
        std::cerr << "Axis " << axis << " > dev_name: " << info.axes[axis].dev_name << " encoder_resolution: " << info.axes[axis].encoder_resolution
                  << " hw_version: " << info.axes[axis].hw_version << " sw_version: " << info.axes[axis].sw_version << std::endl;
    }
}

void print_available_commands() {
    const char kLevelIndent[] = "    ";
    std::cerr << "Available commands:" << std::endl;
    std::cerr << kLevelIndent << "h, help           - print this message" << std::endl;
    std::cerr << kLevelIndent << "q                 - quit" << std::endl;
    std::cerr << kLevelIndent << "s                 - print system status" << std::endl;
    std::cerr << kLevelIndent << "i                 - print system info" << std::endl;
    std::cerr << kLevelIndent << "a|e v <vely>      - set (a)zimuth or (e)levation drive to 'scan' mode with <vel> velocity [pulses/sec]" << std::endl;
    std::cerr << kLevelIndent << "a|e p <pos>       - set (a)zimuth or (e)levation drive to 'point' mode with <pos> position [pulses]" << std::endl;
}

int main(int argc, char* argv[]) {
    blog::trivial::severity_level loglevel;
    fs::path cfg_file_path, log_file_path;
    uint32_t log_rate_us;

    po::options_description options("options");
    options.add_options()
        ("help,h", "display this message")
        ("loglevel,l", po::value<boost::log::trivial::severity_level>(&loglevel)->default_value(boost::log::trivial::warning), "global loglevel (trace, debug, info, warning, error or fatal)")
        ("config,c", po::value<fs::path>(&cfg_file_path)->required()->default_value("servo.conf"), "path to config file")
        ("logfile,f", po::value<fs::path>(&log_file_path), "path to output log file. If specified engine real time data will be written to this file")
        ("lograte,r", po::value<uint32_t>(&log_rate_us), "period in microseconds between samples written to log file. Ignored without 'logfile' option")
    ;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, options), vm);
        po::notify(vm);
    } catch(const po::error& ex) {
        std::cerr << "Failed to parse command line options: " << ex.what() << std::endl;
        std::cerr << options << std::endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help")) {
        std::cerr << options << std::endl;
        return EXIT_FAILURE;
    }

    const char* kLogFormat = "%LineID% %TimeStamp% (%ProcessID%:%ThreadID%) [%Severity%] : %Message%";
    common::InitLogger(loglevel, kLogFormat);

    Config::Storage config;
    try {
        config.ReadFile(cfg_file_path.string());
    } catch(const Config::Exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    Drives::Control control(config);

    std::cerr << "Please, specify your commands here:" << std::endl;

    const boost::atomic<Drives::SystemStatus>& sys_status = control.GetStatus();

    struct StatReader {
        StatReader(const  boost::atomic<Drives::SystemStatus>& status, const fs::path& outfilepath, uint32_t lograte_us)
            : stop_(false)
            , status_(status)
            , outfilepath_(outfilepath)
            , lograte_us_(lograte_us)
        {}

        void CycleRead() {
            if (outfilepath_.empty()) {
                return;
            }
            std::ofstream ofs(outfilepath_.string());
            if (! ofs.good()) {
                std::cerr << "Failed to open log file" << std::endl;
                return;
            }
            while (! stop_) {
                print_status(status_.load(boost::memory_order_acquire), ofs);
                boost::this_thread::sleep_for(boost::chrono::microseconds(lograte_us_));
            }
            ofs.close();
        }

        volatile bool stop_;
        const boost::atomic<Drives::SystemStatus>& status_;
        const fs::path outfilepath_;
        const uint32_t lograte_us_;
    };

    StatReader statreader(sys_status, log_file_path, log_rate_us);
    boost::thread statthread(boost::bind(&StatReader::CycleRead, statreader));

    std::string cmd_str;
    const Drives::SystemInfo& sys_info = control.GetSystemInfo();
    while (true) {
        std::cerr << "> ";
        std::getline(std::cin, cmd_str);

        if (cmd_str == "q") {
            break;
        } else if (cmd_str == "h" || cmd_str == "help") {
            print_available_commands();
            continue;
        } else if (cmd_str == "s") {
            print_status_cerr(sys_status.load(boost::memory_order_acquire));
            continue;
        } else if (cmd_str == "i") {
            print_info(sys_info);
            continue;
        } else if (cmd_str.empty()) {
            continue;
        }

        Command cmd;
        if (! parse_args(cmd_str, cmd)) {
            continue;
        }

        // Команда будет передана, только если флаг соответствующий двигателю будет установлен.
        if (cmd.idle) {
            control.SetModeIdle(cmd.axis);
        } else {
            control.SetModeRun(cmd.axis, cmd.pos, cmd.vel);
        }

        std::cerr << "Command axis: " << cmd.axis << " pos: " << cmd.pos << " vel: " << cmd.vel << " idle: " << cmd.idle << std::endl;
    }

    statreader.stop_ = true;
    statthread.join();

    return 0;
}
