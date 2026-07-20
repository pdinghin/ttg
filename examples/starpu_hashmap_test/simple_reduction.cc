#include <ttg.h>
#include <iostream>
#include <chrono>
#include <getopt.h>
#include <future>
#include <fstream>

#if MEASURE_HASH
    std::atomic_llong global_hash_time{0};
#endif

struct StressKey {
    int64_t id;

    StressKey() = default;
    explicit StressKey(int64_t i) : id(i) {}

    operator int64_t() const { return id; }

    bool operator==(const StressKey& other) const { return id == other.id; }
    bool operator!=(const StressKey& other) const { return id != other.id; }
    bool operator<(const StressKey& other) const { return id < other.id; }

    std::size_t hash() const {
        return 42; 
    }
};

struct NormalKey {
    int64_t id;

    NormalKey() = default;
    explicit NormalKey(int64_t i) : id(i) {}

    operator int64_t() const { return id; }

    bool operator==(const NormalKey& other) const { return id == other.id; }
    bool operator!=(const NormalKey& other) const { return id != other.id; }
    bool operator<(const NormalKey& other) const { return id < other.id; }

    std::size_t hash() const {
        return std::hash<int64_t>{}(id);
    }
};

namespace std {
    template <>
    struct hash<StressKey> {
        std::size_t operator()(const StressKey& k) const {
            return 42; 
        }
    };

    template <>
    struct hash<NormalKey> {
        std::size_t operator()(const NormalKey& k) const {
            return std::hash<int64_t>{}(k.id);
        }
    };
}


template <typename K>
void run_reduction(int64_t H, bool rec, std::string rec_file, std::string name_test) {
    const int64_t N = 1 << H;
    
    ttg::Edge<K, int64_t> left_edge;
    ttg::Edge<K, int64_t> right_edge;
    ttg::Edge<void, int64_t> node_2_pr;

    auto leaf = ttg::make_tt<K>(
        [=](K key, auto& out) {
            int64_t n = key.id;
            K parent_key(n / 2);

            if (n % 2 == 0) {
                ttg::send<0>(parent_key, 1, out); 
            } else {
                ttg::send<1>(parent_key, 1, out);  
            }
        },
        ttg::edges(),
        ttg::edges(left_edge, right_edge), 
        "leaf"
    );

    auto node = ttg::make_tt(
        [=](K key, int64_t left_val, int64_t right_val, auto& out) {
            int64_t n = key.id;
            int64_t sum = left_val + right_val;
            
            if (n > 1) {
                K parent_key(n / 2);
                if (n % 2 == 0) {
                    ttg::send<0>(parent_key, sum, out); 
                } else {
                    ttg::send<1>(parent_key, sum, out); 
                }
            } else {
                ttg::sendv<2>(sum, out); 
            }
        },
        ttg::edges(left_edge, right_edge), 
        ttg::edges(left_edge, right_edge, node_2_pr),
        "node"
    );

    std::promise<int64_t> res_promise;
    auto res_future = res_promise.get_future();

    auto printer = ttg::make_tt(
        [=, &res_promise](int64_t res) {
            res_promise.set_value(res);
            std::cout << "Final result : " << res << " | Expected result : " << N << std::endl;
        },
        ttg::edges(node_2_pr),
        ttg::edges(),
        "printer"
    );

    ttg::make_graph_executable(leaf);

    auto start = std::chrono::high_resolution_clock::now();
    ttg::execute();

    if (ttg::get_default_world().rank() == 0) {
        for (int64_t i = N; i < 2 * N; ++i) {
            leaf->invoke(K(i));
        }
    }
    ttg::fence();
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    #if MEASURE_HASH
        global_hash_time.fetch_add(tl_accumulated_ns,std::memory_order_relaxed);
    #endif
    if (ttg::get_default_world().rank() == 0) {
        int64_t final_res = res_future.get();
        std::cout << "Execution time : " << duration.count() << " ms | Result: " << final_res << " Expected result :" << N << std::endl;
        if(rec){
            std::ofstream outFile(rec_file,std::ios::app);
            #if MEASURE_HASH
                long long avg_hash_time = global_hash_time.load() / ttg::detail::num_threads();
                outFile << ttg::detail::num_threads() << ";" << duration.count() << ";" << avg_hash_time << ";" << final_res << ";" << N << ";" << name_test << "\n";
            #else
                outFile << ttg::detail::num_threads() << ";" << duration.count() << ";" << " "<< ";" << final_res << ";" << N << ";" << name_test << "\n";
            #endif
        }
    }
}

int main(int argc, char *argv[]) {

    static struct option long_options[]{
        {"height", required_argument, 0,'h'},
        {"record", required_argument, 0, 'r'},
        {"name-test", required_argument, 0, 'n'},
        {"key", required_argument, 0, 'k'},
        {0,0,0,0}
    };

    int64_t H = 2;
    bool rec = false;
    std::string rec_file;
    std::string name_test = "ttg_starpu";
    std::string key_type = "normal";

    int opt;
    while((opt = getopt_long(argc,argv,"h:r:k:n:",long_options,nullptr)) != -1){
        switch(opt){
            case 'h':
                H = atoi(optarg);
                break;
            case 'r':
                rec_file = optarg;
                rec = true;
                break;
            case 'n':
                name_test = optarg;
                break;
            case 'k':
                key_type = optarg;
                break;
            default:
                break;
        }
    }

    const int64_t N = 1 << H; 
    std::cout << "Number of leaf : " << N <<  " Number of threads : " << ttg::detail::num_threads() << std::endl;
    ttg::initialize(argc, argv, -1);
    
    if (key_type == "stress") {
        run_reduction<StressKey>(H, rec, rec_file, name_test);
    } else {
        run_reduction<NormalKey>(H, rec, rec_file, name_test);
    }

    ttg::finalize();
    return 0;
}
