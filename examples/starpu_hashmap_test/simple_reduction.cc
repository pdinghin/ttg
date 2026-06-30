#include <ttg.h>
#include <iostream>
#include <chrono>
#include <getopt.h>
#include <future>
#include <fstream>

struct BadKey {
    int64_t id;

    BadKey() = default;
    explicit BadKey(int64_t i) : id(i) {}

    operator int64_t() const { return id; }

    bool operator==(const BadKey& other) const { return id == other.id; }
    bool operator!=(const BadKey& other) const { return id != other.id; }
    bool operator<(const BadKey& other) const { return id < other.id; }

    std::size_t hash() const {
        return 42; 
    }
};

namespace std {
    template <>
    struct hash<BadKey> {
        std::size_t operator()(const BadKey& k) const {
            return 42; 
        }
    };
}

int main(int argc, char *argv[]) {

    static struct option long_options[]{
        {"height", required_argument, 0,'h'},
        {"record", required_argument, 0, 'r'},
        {0,0,0,0}
    };

    int64_t H = 2;
    bool rec = false;
    std::string rec_file;
    int opt;
    while((opt = getopt(argc,argv,"h:r:")) != -1){
        switch(opt){
            case 'h':
                H = atoi(optarg);
                break;
            case 'r':
                rec_file = optarg;
                rec = true;
                break;
            default:
                break;
        }
    }

    const int64_t N = 1 << H; 
    std::cout << "Number of leaf : " << N <<  " Number of threads : " << std::getenv("TTG_NUM_THREADS") << std::endl;
    ttg::initialize(argc, argv, -1);
    

    ttg::Edge<BadKey, int64_t> left_edge;
    ttg::Edge<BadKey, int64_t> right_edge;
    ttg::Edge<void, int64_t> node_2_pr;

    auto leaf = ttg::make_tt<BadKey>(
        [=](BadKey key, auto& out) {
            int64_t n = key.id;
            BadKey parent_key(n / 2);

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
        [=](BadKey key, int64_t left_val, int64_t right_val, auto& out) {
            int64_t n = key.id;
            int64_t sum = left_val + right_val;
            
            if (n > 1) {
                BadKey parent_key(n / 2);
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
            leaf->invoke(BadKey(i));
        }
    }
    ttg::fence();
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;

    if (ttg::get_default_world().rank() == 0) {
        int64_t final_res = res_future.get();
        std::cout << "Execution time : " << duration.count() << " ms | Result: " << final_res << " Expected result :" << N << std::endl;
        if(rec){
            std::ofstream outFile(rec_file,std::ios::app);
            outFile << std::getenv("TTG_NUM_THREADS") << ";" << duration.count() << ";" << final_res << ";" << N << "\n";
        }
    }

    ttg::finalize();
    return 0;
}