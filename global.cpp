#include <iostream>
#include <unordered_map>
#include <queue>
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
    bool isEmpty;
    int memoryFrames;
    vector<pair<int, uint64_t>> memory; // Tracks which frame is used by which page
    int nextFrame;
    list<int> lru_list; // Keeps track of the order of accesses for LRU

    FrameStatus(int memoryF)
        : isEmpty(true), memoryFrames(memoryF), nextFrame(0) {
        memory = vector<pair<int, uint64_t>>(memoryF, {-1, 0}); // Initialize all frames as free
    }

    // Allocates a frame if available
    int allocateFrame(int process_id, uint64_t page_number) {
        if (memory[nextFrame].first == -1) {
            // Assign the next available frame
            memory[nextFrame] = {process_id, page_number};
            lru_list.push_back(nextFrame); // Add to LRU tracking
            int temp = nextFrame;
            nextFrame = (nextFrame + 1) % memoryFrames;
            return temp;
        }
        return -1; // No free frame available
    }

    // Releases a frame, making it available for allocation
    void releaseFrame(int frame_id) {
        memory[frame_id] = {-1, 0}; // Reset the frame as free
        lru_list.remove(frame_id);   // Remove from LRU tracking
    }

    // FIFO Replacement Policy (evicts the oldest page)
    tuple<int, uint64_t, int> fifoReplacement(int process_id, uint64_t page_number) {
        // Find the oldest frame (first one assigned)
        int frame_id = nextFrame;
        int old_pid = memory[frame_id].first;
        uint64_t old_page_no = memory[frame_id].second;

        // Evict the page in the oldest frame
        releaseFrame(frame_id);

        // Increment the nextFrame for the next eviction
        nextFrame = (nextFrame + 1) % memoryFrames;

        // Assign new page to the evicted frame
        memory[frame_id] = {process_id, page_number};

        // Return the evicted details
        return make_tuple(old_pid, old_page_no, frame_id);
    }

    // LRU Replacement Policy (evicts the least recently used page)
    tuple<int, uint64_t, int> lruReplacement(int process_id, uint64_t page_number) {
        // Find the least recently used frame (first in the list)
        int frame_id = lru_list.front();
        lru_list.pop_front();

        int old_pid = memory[frame_id].first;
        uint64_t old_page_no = memory[frame_id].second;

        // Evict the page in the least recently used frame
        releaseFrame(frame_id);

        // Assign new page to the evicted frame
        memory[frame_id] = {process_id, page_number};

        // Add the frame to the back of the LRU list (most recently used)
        lru_list.push_back(frame_id);

        // Return the evicted details
        return make_tuple(old_pid, old_page_no, frame_id);
    }

    // Optimal Replacement Policy (evicts the page that will not be used for the longest time)
    tuple<int, uint64_t, int> optimalReplacement(int process_id, uint64_t page_number, vector<pair<int, uint64_t>>& trace, int current_index) {
        // Find the page that will be used the farthest in the future
        int farthest_frame = -1;
        int farthest_index = -1;
        int old_pid = -1;
        uint64_t old_page_no = -1;

        // Iterate through frames to find the farthest page in the future
        for (int i = 0; i < memoryFrames; ++i) {
            int frame_pid = memory[i].first;
            uint64_t frame_page = memory[i].second;
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
                // If a page won't be used in the future, evict it immediately
                farthest_frame = i;
                old_pid = frame_pid;
                old_page_no = frame_page;
                break;
            }
        }

        // Evict the selected page
        releaseFrame(farthest_frame);

        // Assign the new page to the evicted frame
        memory[farthest_frame] = {process_id, page_number};

        // Return the evicted details
        return make_tuple(old_pid, old_page_no, farthest_frame);
    }

    // Random Replacement Policy (evicts a random page)
    tuple<int, uint64_t, int> randomReplacement(int process_id, uint64_t page_number) {
        // Select a random frame index to evict
        int random_frame = rand() % memoryFrames;
        int old_pid = memory[random_frame].first;
        uint64_t old_page_no = memory[random_frame].second;

        // Evict the page in the randomly selected frame
        releaseFrame(random_frame);

        // Assign new page to the evicted frame
        memory[random_frame] = {process_id, page_number};

        // Return the evicted details
        return make_tuple(old_pid, old_page_no, random_frame);
    }

    // Update LRU order
    void updateLRU(int frame_id) {
        lru_list.remove(frame_id);   // Remove from current position
        lru_list.push_back(frame_id); // Move to back (most recently used)
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
        : page_size(ps), num_frames(nf), replacement_policy(policy), frame_status(FrameStatus(nf)) {
        for (int i = 0; i < 4; ++i) {
            page_tables.emplace_back(PageTable(i)); // Create page table for each process
        }
    }

    // Loads and processes the memory trace file
    vector<pair<int, uint64_t>> loadMemoryTrace(const string& file_path) {
        ifstream trace_file(file_path);
        string line;
        vector<pair<int, uint64_t>> trace_entries;

        // Read all entries from the file
        while (getline(trace_file, line)) {
            int process_id;
            uint64_t virtual_address;
            sscanf(line.c_str(), "%d,%lu", &process_id, &virtual_address);
            uint64_t page_number = virtual_address >> static_cast<int>(log2(page_size)); // Compute page number from virtual address
            trace_entries.push_back({process_id, page_number});
        }
        return trace_entries;
    }

    // Handles an access to a page by a process
    void handleAccess(int process_id, uint64_t page_number, vector<pair<int, uint64_t>>& trace, int current_index) {
        PageTable& page_table = page_tables[process_id];

        // Check if the page is already in memory
        if (page_table.page_to_frame.find(page_number) != page_table.page_to_frame.end()) {
            // Page hit: update LRU if applicable
            if (replacement_policy == "lru") {
                int frame_id = page_table.page_to_frame[page_number];
                frame_status.updateLRU(frame_id);
            }
            return; // Page hit, no page fault
        }

        // Page fault handling
        global_page_fault_count++;
        page_table.page_fault_count++;

        // Print the page fault for the current process
        //cout << "Page fault for process " << process_id << " on page " << page_number << endl;

        int frame = frame_status.allocateFrame(process_id, page_number);
        if (frame == -1) {
            // No free frame available, apply the selected replacement policy
            if (replacement_policy == "fifo") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.fifoReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
                //cout << "Evicting process " << old_pid << " page " << old_page_no << " from frame " << evicted_frame << endl;
            }
            else if (replacement_policy == "lru") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.lruReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
                //cout << "Evicting process " << old_pid << " page " << old_page_no << " from frame " << evicted_frame << endl;
            }
            else if (replacement_policy == "optimal") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.optimalReplacement(process_id, page_number, trace, current_index);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
                //cout << "Evicting process " << old_pid << " page " << old_page_no << " from frame " << evicted_frame << endl;
            }
            else if (replacement_policy == "random") {
                auto [old_pid, old_page_no, evicted_frame] = frame_status.randomReplacement(process_id, page_number);
                page_tables[old_pid].page_to_frame.erase(old_page_no);
                page_tables[process_id].page_to_frame[page_number] = evicted_frame;
                //cout << "Evicting process " << old_pid << " page " << old_page_no << " from frame " << evicted_frame << endl;
            }
        } else {
            // Frame allocation successful, no eviction
            page_tables[process_id].page_to_frame[page_number] = frame;
        }
    }

    // Run the simulation
    void runSimulation(const string& trace_file) {
    vector<pair<int, uint64_t>> trace = loadMemoryTrace(trace_file);

    // Process all the accesses
    for (int i = 0; i < trace.size(); ++i) {
        handleAccess(trace[i].first, trace[i].second, trace, i);
    }

    // After processing all accesses, print the results
    cout << "Global page fault count: " << global_page_fault_count << endl;

    // Print individual page fault counts for each process
    for (int i = 0; i < page_tables.size(); ++i) {
        cout << "Process " << i << " page fault count: " << page_tables[i].page_fault_count << endl;
    }
}

};

int main(int argc, char* argv[]) {
    // Check if enough arguments are passed
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <page_size> <num_frames> <replacement_policy> <trace_file>" << std::endl;
        return 1; // Exit with error
    }

    string trace_file = argv[4];                  // Trace file path
    int page_size = std::atoi(argv[1]);           // Page size (convert string to int)
    int num_frames = std::atoi(argv[2]);          // Number of frames
    string replacement_policy = argv[3];          // Replacement policy (e.g., "lru")

    // Create the VirtualMemoryManager with arguments
    VirtualMemoryManager manager(page_size, num_frames, replacement_policy);

    // Run the simulation
    manager.runSimulation(trace_file);

    return 0;
}

