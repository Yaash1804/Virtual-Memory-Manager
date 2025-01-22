#include <iostream>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <string>
#include <climits>
#include <cmath>
#include <tuple>
#include <list>
#include <cstdlib>  // For rand() and srand()

using namespace std;

class PageTable {
public:
    unordered_map<uint64_t, int> page_to_frame; // Mapping from page to frame
    int process_id; // ID of the process
    int page_fault_count = 0; // Counter for page faults for this process
    PageTable(int pid) : process_id(pid) {}
};

class FrameStatus {
public:
    int memoryFramesPerProcess;
    vector<vector<pair<int, uint64_t>>> memory; // Process-specific memory frames
    vector<int> nextFrame; // Next frame to replace per process (for FIFO)
    vector<list<int>> lru_list; // LRU tracking per process

    FrameStatus(int memoryF, int num_processes)
        : memoryFramesPerProcess(memoryF / num_processes), 
          nextFrame(num_processes, 0), 
          lru_list(num_processes) {
        // Initialize frames and tracking lists for each process
        memory.resize(num_processes, vector<pair<int, uint64_t>>(memoryFramesPerProcess, {-1, 0}));
    }

    // Allocates a frame for a specific process if available
    int allocateFrame(int process_id, uint64_t page_number) {
        for (int i = 0; i < memoryFramesPerProcess; ++i) {
            if (memory[process_id][i].first == -1) { // Check for an empty frame
                memory[process_id][i] = {process_id, page_number};
                lru_list[process_id].push_back(i); // Add to LRU tracking
                return i;
            }
        }
        return -1; // No free frame available for the process
    }

    // Releases a frame for a specific process, making it available for allocation
    void releaseFrame(int process_id, int frame_id) {
        memory[process_id][frame_id] = {-1, 0}; // Reset the frame as free
        lru_list[process_id].remove(frame_id);   // Remove from LRU tracking
    }

    // FIFO Replacement Policy for a specific process
    tuple<int, uint64_t, int> fifoReplacement(int process_id, uint64_t page_number) {
        int frame_id = nextFrame[process_id];
        int old_pid = memory[process_id][frame_id].first;
        uint64_t old_page_no = memory[process_id][frame_id].second;

        releaseFrame(process_id, frame_id);
        nextFrame[process_id] = (frame_id + 1) % memoryFramesPerProcess;
        memory[process_id][frame_id] = {process_id, page_number};

        return make_tuple(old_pid, old_page_no, frame_id);
    }

    // LRU Replacement Policy for a specific process
    tuple<int, uint64_t, int> lruReplacement(int process_id, uint64_t page_number) {
        int frame_id = lru_list[process_id].front();
        lru_list[process_id].pop_front();
        int old_pid = memory[process_id][frame_id].first;
        uint64_t old_page_no = memory[process_id][frame_id].second;

        releaseFrame(process_id, frame_id);
        memory[process_id][frame_id] = {process_id, page_number};
        lru_list[process_id].push_back(frame_id);

        return make_tuple(old_pid, old_page_no, frame_id);
    }

    // Optimal Replacement Policy for a specific process
    tuple<int, uint64_t, int> optimalReplacement(int process_id, uint64_t page_number, vector<pair<int, uint64_t>>& trace, int current_index) {
        int farthest_frame = -1;
        int farthest_index = -1;
        int old_pid = -1;
        uint64_t old_page_no = -1;

        for (int i = 0; i < memoryFramesPerProcess; ++i) {
            int frame_pid = memory[process_id][i].first;
            uint64_t frame_page = memory[process_id][i].second;
            bool found_in_future = false;

            for (int j = current_index + 1; j < trace.size(); ++j) {
                if (trace[j].first == frame_pid && trace[j].second == frame_page) {
                    found_in_future = true;
                    if (farthest_index < j) {
                        farthest_index = j;
                        farthest_frame = i;
                        old_pid = frame_pid;
                        old_page_no = frame_page;
                    }
                    break;
                }
            }

            if (!found_in_future) {
                farthest_frame = i;
                old_pid = frame_pid;
                old_page_no = frame_page;
                break;
            }
        }

        releaseFrame(process_id, farthest_frame);
        memory[process_id][farthest_frame] = {process_id, page_number};

        return make_tuple(old_pid, old_page_no, farthest_frame);
    }

    // Random Replacement Policy for a specific process
    tuple<int, uint64_t, int> randomReplacement(int process_id, uint64_t page_number) {
        int random_frame = rand() % memoryFramesPerProcess;
        int old_pid = memory[process_id][random_frame].first;
        uint64_t old_page_no = memory[process_id][random_frame].second;

        releaseFrame(process_id, random_frame);
        memory[process_id][random_frame] = {process_id, page_number};

        return make_tuple(old_pid, old_page_no, random_frame);
    }
};

class VirtualMemoryManager {
    int page_size;
    int num_frames;
    string replacement_policy;
    vector<PageTable> page_tables;
    FrameStatus frame_status;
    int global_page_fault_count = 0;

public:
    VirtualMemoryManager(int ps, int nf, const string& policy)
        : page_size(ps), num_frames(nf), replacement_policy(policy), frame_status(FrameStatus(nf, 4)) {
        for (int i = 0; i < 4; ++i) {
            page_tables.emplace_back(PageTable(i));
        }
    }

    vector<pair<int, uint64_t>> loadMemoryTrace(const string& file_path) {
        ifstream trace_file(file_path);
        string line;
        vector<pair<int, uint64_t>> trace_entries;

        while (getline(trace_file, line)) {
            int process_id;
            uint64_t virtual_address;
            sscanf(line.c_str(), "%d,%lu", &process_id, &virtual_address);
            uint64_t page_number = virtual_address >> static_cast<int>(log2(page_size));
            trace_entries.push_back({process_id, page_number});
        }
        return trace_entries;
    }

    void handleAccess(int process_id, uint64_t page_number, vector<pair<int, uint64_t>>& trace, int current_index) {
        PageTable& page_table = page_tables[process_id];

        if (page_table.page_to_frame.find(page_number) != page_table.page_to_frame.end()) {
            if (replacement_policy == "lru") {
                int frame_id = page_table.page_to_frame[page_number];
                frame_status.lru_list[process_id].remove(frame_id);
                frame_status.lru_list[process_id].push_back(frame_id);
            }
            return;
        }

        global_page_fault_count++;
        page_table.page_fault_count++;

        int frame = frame_status.allocateFrame(process_id, page_number);
        if (frame == -1) {
            if (replacement_policy == "fifo") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.fifoReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
            } else if (replacement_policy == "lru") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.lruReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
            } else if (replacement_policy == "optimal") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.optimalReplacement(process_id, page_number, trace, current_index);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
            } else if (replacement_policy == "random") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.randomReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
            }
        } else {
            page_tables[process_id].page_to_frame[page_number] = frame;
        }
    }

    void runSimulation(const string& trace_file) {
        vector<pair<int, uint64_t>> trace = loadMemoryTrace(trace_file);
        for (int i = 0; i < trace.size(); ++i) {
            handleAccess(trace[i].first, trace[i].second, trace, i);
        }

        cout << "Global page fault count: " << global_page_fault_count << endl;
        for (int i = 0; i < page_tables.size(); ++i) {
            cout << "Process " << i << " page fault count: " << page_tables[i].page_fault_count << endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cerr << "Usage: " << argv[0] << " <page_size> <num_frames> <replacement_policy> <trace_file>" << endl;
        return 1;
    }

    string trace_file = argv[4];
    int page_size = atoi(argv[1]);
    int num_frames = atoi(argv[2]);
    string replacement_policy = argv[3];
    
    VirtualMemoryManager manager(page_size, num_frames, replacement_policy);
    manager.runSimulation(trace_file);

    return 0;
}
