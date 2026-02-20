#include <docwire/ai_runner.h>
#include <docwire/docwire.h>
#include <docwire/llama_runner.h>
#include <docwire/model_inference_config.h>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char* argv[])
{
    using namespace docwire;
    std::stringstream out_stream;
    docwire::local_ai::model_inference_config config;
    config.model_path = "../models/qwen2-7b-instruct-q4_k_m.gguf";
    config.max_tokens = docwire::local_ai::token_limit{256};
    config.n_ctx = docwire::local_ai::context_size{4096};
    config.n_threads = docwire::local_ai::thread_count{4};
    config.temp = docwire::local_ai::temperature{0.2f};
    config.min_probability = docwire::local_ai::min_p{0.05f};
    auto runner = std::make_shared<docwire::local_ai::llama_runner>(config);

    try {

        data_source(std::string("LLMs help process long documents."), mime_type{"text/plain"},
                    confidence::highest) |
            local_ai::model_chain_element("Summarize:\n\n", runner) | out_stream;

        //  Write to a text file
        std::ofstream ofs("output.txt");
        ofs << out_stream.str();
        ofs.close();

        std::cout << "Text exported to output.txt" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << errors::diagnostic_message(e) << std::endl;
        return 1;
    }

    return 0;
}
