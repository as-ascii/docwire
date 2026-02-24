/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge
 * Community Choice & Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost
 * efficiency in text extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and
 * confidentiality                                                          */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com */
/*  Project homepage: https://github.com/docwire/docwire */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/

#include "llama_runner.h"
#include "error_tags.h"
#include "llama_handler.h"
#include "throw_if.h"
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <llama.h>
#include <mutex>

namespace docwire
{

namespace
{

std::mutex llama_backend_mutex;
std::condition_variable llama_backend_cv;
std::size_t runner_count = 0;
std::size_t active_calls = 0;
bool g_verbose = false;
/**
 * @brief Manages global lifetime of llama.cpp backend.
 *
 * llama.cpp requires explicit global initialization and teardown via:
 *   - llama_backend_init()
 *   - llama_backend_free()
 *
 * This guard implements a reference-counted lifetime model:
 *
 * - The first live llama_runner initializes the backend.
 * - The last destroyed llama_runner frees the backend.
 *
 * Thread Safety:
 * - Protected by a global mutex.
 * - Teardown waits for all active inference calls to complete.
 *
 * This prevents undefined behavior if a runner is destroyed while
 * another thread is still performing inference.
 */
struct llama_backend_guard
{
    llama_backend_guard()
    {
        std::lock_guard<std::mutex> lock(llama_backend_mutex);
        if (runner_count++ == 0) {
            llama_backend_init();
        }
    }

    ~llama_backend_guard()
    {
        std::unique_lock<std::mutex> lock(llama_backend_mutex);
        if (--runner_count == 0) {
            llama_backend_cv.wait(lock, [] { return active_calls == 0; });
            llama_backend_free();
        }
    }

    static void acquire_call()
    {
        std::lock_guard<std::mutex> lock(llama_backend_mutex);
        ++active_calls;
    }

    static void release_call()
    {
        std::lock_guard<std::mutex> lock(llama_backend_mutex);
        if (--active_calls == 0 && runner_count == 0) {
            llama_backend_cv.notify_all();
        }
    }
};
/**
 * @brief Tracks active inference calls.
 *
 * Each call to llama_runner::process() creates a llama_call_guard.
 *
 * Responsibilities:
 * - Increments the global active_calls counter on entry.
 * - Decrements it on exit.
 *
 * This ensures that backend teardown is deferred until all
 * in-flight llama_decode() calls have completed.
 *
 * Used together with llama_backend_guard to provide safe
 * concurrent inference and deterministic backend shutdown.
 */

struct llama_call_guard
{
    llama_call_guard() { llama_backend_guard::acquire_call(); }
    ~llama_call_guard() { llama_backend_guard::release_call(); }
};

} // anonymous namespace

// static bool g_verbose = false;

template <> struct pimpl_impl<local_ai::llama_runner> : pimpl_impl_base
{
    llama_backend_guard llama_backend;
    local_ai::model_inference_config config;
    local_ai::llama_handle<llama_model> model;
    local_ai::llama_handle<llama_context> ctx;
    local_ai::llama_handle<llama_sampler> sampler;

    static void llamaLogCallback(ggml_log_level level, const char* text, void* /*user*/)
    {
        if (g_verbose || level == GGML_LOG_LEVEL_ERROR) {
            std::cerr << text;
        }
    }
    pimpl_impl(const local_ai::model_inference_config& cfg) : config(cfg)
    {
        g_verbose = config.verbose;
        //  Redirect llama.cpp's logs through our callback
        llama_log_set(llamaLogCallback, nullptr);

        llama_model_params model_params = llama_model_default_params();
        // load model to Llama
        model = docwire::local_ai::llama_handle<llama_model>(
            llama_model_load_from_file(config.model_path.c_str(), model_params));

        throw_if(!model, "Failed to load llama model.", errors::program_corrupted{});
        // Set up context and other parameters
        llama_context_params ctx_params = llama_context_default_params();

        ctx_params.n_ctx = config.n_ctx.get();
        ctx_params.n_batch = 512;
        ctx_params.n_threads = config.n_threads.get();
        ctx_params.embeddings = true;
        ctx = docwire::local_ai::llama_handle<llama_context>(
            llama_init_from_model(model.get(), ctx_params));

        throw_if(!ctx, "Failed to create llama context.", errors::program_corrupted{});

        llama_sampler_chain_params sp = llama_sampler_chain_default_params();

        sampler = docwire::local_ai::llama_handle<llama_sampler>(llama_sampler_chain_init(sp));
        throw_if(!sampler, "Failed to create sampler.", errors::program_corrupted{});

        llama_sampler_chain_add(sampler.get(),
                                llama_sampler_init_min_p(config.min_probability.get(), 1));

        llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(config.temp.get()));

        llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    ~pimpl_impl() {}

    void reset()
    {
        // Get the memory handle first
        llama_memory_t mem = llama_get_memory(ctx.get());
        // Then clear all sequences
        llama_memory_seq_rm(mem, -1, -1, -1);
        llama_sampler_reset(sampler.get());
    }
};

namespace local_ai
{
llama_runner::llama_runner(const model_inference_config& config) : with_pimpl(config) {}

/*
 * This function runs inference on the given model provided to Llama
 */
std::string llama_runner::process(const std::string& input)
{
    llama_call_guard guard;
    auto& impl = this->impl();

    impl.reset();

    const llama_vocab* vocab = llama_model_get_vocab(impl.model.get());

    std::vector<llama_token> tokens(input.size());

    int n_tokens = llama_tokenize(vocab, input.c_str(), input.length(), tokens.data(),
                                  tokens.size(), true, false);

    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    throw_if(llama_decode(impl.ctx.get(), batch) != 0, "Decode failed", errors::program_logic{});

    std::string output;

    for (int i = 0; i < impl.config.max_tokens.get(); ++i) {
        llama_token token = llama_sampler_sample(impl.sampler.get(), impl.ctx.get(), -1);

        llama_sampler_accept(impl.sampler.get(), token);

        if (llama_vocab_is_eog(vocab, token))
            break;

        char buf[256];
        int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, true);

        output.append(buf, n);

        batch = llama_batch_get_one(&token, 1);

        if (llama_decode(impl.ctx.get(), batch) != 0)
            break;
    }
    // std::cout << output << std::endl;
    return output;
}

/**
 * @brief Generates an embedding vector for the given input string.
 */
std::vector<double> llama_runner::embed(const std::string& input)
{
    llama_call_guard guard;
    auto& impl = this->impl();

    impl.reset();

    throw_if(llama_model_n_embd(impl.model.get()) <= 0, "Model has no embedding dimension.",
             errors::program_logic{});

    const llama_vocab* vocab = llama_model_get_vocab(impl.model.get());

    int n_tokens = llama_tokenize(vocab, input.c_str(), input.length(), nullptr, 0, true, false);

    throw_if(n_tokens <= 0, "Cannot embed empty input.", errors::program_logic{});

    std::vector<llama_token> tokens(n_tokens);

    int written = llama_tokenize(vocab, input.c_str(), input.length(), tokens.data(), tokens.size(),
                                 true, false);

    throw_if(written != n_tokens, "Tokenization mismatch.", errors::program_logic{});

    llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());

    throw_if(llama_decode(impl.ctx.get(), batch) != 0, "Decode failed during embedding.",
             errors::program_logic{});

    const float* all_embeddings = llama_get_embeddings(impl.ctx.get());

    throw_if(!all_embeddings, "Embeddings not available from model.", errors::program_logic{});

    const int n_embd = llama_model_n_embd(impl.model.get());

    std::vector<double> result(n_embd, 0.0);

    //  Mean pooling
    for (int t = 0; t < n_tokens; ++t) {
        const float* token_emb = all_embeddings + t * n_embd;

        for (int i = 0; i < n_embd; ++i) {
            result[i] += static_cast<double>(token_emb[i]);
        }
    }

    for (double& v : result) {
        v /= static_cast<double>(n_tokens);
    }

    // L2 normalization
    double norm = 0.0;
    for (double v : result)
        norm += v * v;

    norm = std::sqrt(norm);

    if (norm > 1e-6) {
        for (double& v : result)
            v /= norm;
    }

    return result;
}

} // namespace local_ai

} // namespace docwire
