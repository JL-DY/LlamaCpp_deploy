#include "arg.h"
#include "common.h"
#include "console.h"
#include "log.h"
#include "sampling.h"
#include "llama.h"
#include "chat.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "param_json.hpp"
#include <sys/time.h>  

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include <signal.h>
#include <unistd.h>
#elif defined (_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <signal.h>
#endif

#if defined(_MSC_VER)
#pragma warning(disable: 4244 4267) // possible loss of data
#endif

// static llama_context           ** g_ctx;
// static llama_model             ** g_model;
// static common_sampler          ** g_smpl;
// static common_params            * g_params;
static std::vector<llama_token> * g_input_tokens;
static std::ostringstream       * g_output_ss;
static std::vector<llama_token> * g_output_tokens;
static bool is_interacting  = false;

static bool file_exists(const std::string & path) {
    std::ifstream f(path.c_str());
    return f.good();
}

static bool file_is_empty(const std::string & path) {
    std::ifstream f;
    f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    f.open(path.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
    return f.tellg() == 0;
}

inline double GetCurrentUS() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return 1e+6 * time.tv_sec + time.tv_usec;
}

int main(int argc, char ** argv) {
    if (argc != 5) {
        std::cout << "please input:\n"
                  << "model.gguf\n"
                  << "thread\n"
                  << "prompt_path\n"
                  << "param.json" << std::endl;
        return 0;
    }

    //init part
    double start, duration;
    std::vector<Iaa_Param_Inter> result;
    ParamJson param_json(argv[4]);
    param_json.GetParam();

    common_params params;
    params.model.path = argv[1];
    params.cpuparams.n_threads = atoi(argv[2]);
    params.cpuparams_batch.n_threads = atoi(argv[2]);
    params.path_prompt_cache = argv[3];
    params.interactive = true;
   
    //g_params = &params;
    start = GetCurrentUS();
    common_init();

    auto & sparams = params.sampling;

    llama_backend_init();
    llama_numa_init(params.numa); //禁用ggml的numa

    llama_model * model = nullptr;
    llama_context * ctx = nullptr;
    common_sampler * smpl = nullptr;

    //g_model = &model;
    //g_ctx = &ctx;
    //g_smpl = &smpl;

    std::vector<common_chat_msg> chat_msgs;

    common_init_result llama_init = common_init_from_params(params);
    model = llama_init.model.get();
    ctx = llama_init.context.get();

    if (model == NULL) {
        LOG_ERR("%s: error: unable to load model\n", __func__);
        return 1;
    }

    auto * mem = llama_get_memory(ctx);

    smpl = common_sampler_init(model, sparams);
    if (!smpl) {
        LOG_ERR("%s: failed to initialize sampling subsystem\n", __func__);
        return 1;
    }
    // LOG_INF("sampler seed: %u\n",     common_sampler_get_seed(smpl));
    // LOG_INF("sampler params: \n%s\n", sparams.print().c_str());
    // LOG_INF("sampler chain: %s\n",    common_sampler_print(smpl).c_str());

    const llama_vocab * vocab = llama_model_get_vocab(model); //获取词汇表
    auto chat_templates = common_chat_templates_init(model, params.chat_template); //获取chat的特殊字符

    // auto * cpu_dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    // if (!cpu_dev) {
    //     LOG_ERR("%s: no CPU backend found\n", __func__);
    //     return 1;
    // }
    // auto * reg = ggml_backend_dev_backend_reg(cpu_dev);
    // auto * ggml_threadpool_new_fn = (decltype(ggml_threadpool_new) *) ggml_backend_reg_get_proc_address(reg, "ggml_threadpool_new");
    // auto * ggml_threadpool_free_fn = (decltype(ggml_threadpool_free) *) ggml_backend_reg_get_proc_address(reg, "ggml_threadpool_free");

    // struct ggml_threadpool_params tpp = ggml_threadpool_params_from_cpu_params(params.cpuparams);

    set_process_priority(params.cpuparams.priority);

    // struct ggml_threadpool * threadpool = ggml_threadpool_new_fn(&tpp);
    // if (!threadpool) {
    //     LOG_ERR("%s: threadpool create failed : n_threads %d\n", __func__, tpp.n_threads);
    //     return 1;
    // }

    const int n_ctx_train = llama_model_n_ctx_train(model);
    const int n_ctx = llama_n_ctx(ctx);

    if (n_ctx > n_ctx_train) {
        LOG_WRN("%s: model was trained on only %d context tokens (%d specified)\n", __func__, n_ctx_train, n_ctx);
    }

    // 如果能获取到chat的特殊字符,并且模式是auto(如果设置了-cnv或-no-cnv就不会自动转了),则自动转为对话,否则转为文本生成
    const bool has_chat_template = common_chat_templates_was_explicit(chat_templates.get());
    if (params.conversation_mode == COMMON_CONVERSATION_MODE_AUTO) {
        if (has_chat_template) {
            LOG_INF("%s: chat template is available, enabling conversation mode (disable it with -no-cnv)\n", __func__);
            params.conversation_mode = COMMON_CONVERSATION_MODE_ENABLED;
        } else {
            params.conversation_mode = COMMON_CONVERSATION_MODE_DISABLED;
        }
    }

    // 对话模式下,但没有拿到chat的特殊字符
    if (params.conversation_mode && !has_chat_template) {
        LOG_WRN("%s: chat template is not available or is not supported. This may cause the model to output suboptimal responses\n", __func__);
    }

    //负责根据传入的role和content,按照chat_templates的特殊格式进行组合排列
    auto chat_add_and_format = [&chat_msgs, &chat_templates](const std::string & role, const std::string & content) {
        common_chat_msg new_msg;
        new_msg.role = role;
        new_msg.content = content;
        auto formatted = common_chat_format_single(chat_templates.get(), chat_msgs, new_msg, role == "user", false);
        chat_msgs.push_back(new_msg);
        return formatted;
    };

    duration = GetCurrentUS()-start;
    std::cout << "load model use time:" << duration/1000 << std::endl;

    std::string path_session = params.path_prompt_cache;
    std::vector<llama_token> session_tokens;
    std::string prompt;
    //std::vector<llama_token> embd_inp;
    std::vector<llama_token> embd;

    //std::vector<int>   input_tokens;  g_input_tokens  = &input_tokens; //暂时不太清楚这几个有什么用,虽然g_*是全局变量,但是注释了好像也没啥影响,先留着
    //std::vector<int>   output_tokens; g_output_tokens = &output_tokens;
    //std::ostringstream output_ss;     g_output_ss     = &output_ss;
    std::ostringstream assistant_ss; // for storing current assistant message, used in conversation mode

    const int ga_n = params.grp_attn_n; //default 1
    const int ga_w = params.grp_attn_w; //default 512

    int n_past             = 0;
    bool unit_mode = false; //false is control,true is chat

    //加载prompt,prompt缓存文件不存在时会自动生成并保存
    start = GetCurrentUS();
    if (!path_session.empty()) {
        if (!file_exists(path_session) || file_is_empty(path_session)) {
            LOG_INF("%s: session file does not exist or is empty, will create.\n", __func__);
            chat_add_and_format("system", param_json.ai_prompt);
            common_chat_templates_inputs inputs;
            inputs.use_jinja = params.use_jinja;
            inputs.messages = chat_msgs;
            inputs.add_generation_prompt = !params.prompt.empty();
            prompt = common_chat_templates_apply(chat_templates.get(), inputs).prompt;

            LOG_DBG("new prompt is:%s\n", prompt.c_str());
            session_tokens = common_tokenize(ctx, prompt, true, true);
            
            if(session_tokens.size()>=params.n_batch){
                LOG_ERR("The prompt is too long and has exceeded n_batch, currently n_batch is %d", params.n_batch);
            }
            for(int i=0; i<session_tokens.size(); i++){
                //input_tokens.push_back(session_tokens[i]);
                common_sampler_accept(smpl, session_tokens[i], /* accept_grammar= */ false);
            }
            common_sampler_reset(smpl);
            for (int i = 0; i < (int) session_tokens.size(); i += params.n_batch) {
                int n_eval = (int) session_tokens.size() - i;
                if (n_eval > params.n_batch) {
                    n_eval = params.n_batch;
                }
                if (llama_decode(ctx, llama_batch_get_one(&session_tokens[i], n_eval))) {
                    LOG_ERR("%s : failed to eval\n", __func__);
                    return 1;
                }
            }
            n_past = (int) session_tokens.size();
            llama_state_save_file(ctx, path_session.c_str(), session_tokens.data(), session_tokens.size());
            LOG_INF("saved session to %s\n", path_session.c_str());
        } else {
            session_tokens.resize(n_ctx);
            size_t n_token_count_out = 0;
            if (!llama_state_load_file(ctx, path_session.c_str(), session_tokens.data(), session_tokens.capacity(), &n_token_count_out)) {
                LOG_ERR("%s: failed to load session file '%s'\n", __func__, path_session.c_str());
                return -1;
            }
            session_tokens.resize(n_token_count_out);
            if(n_token_count_out>=params.n_batch){
                LOG_ERR("The prompt is too long and has exceeded n_batch, currently n_batch is %d", params.n_batch);
                return -1;
            }
            for(int i=0; i<session_tokens.size(); i++){
                //input_tokens.push_back(session_tokens[i]);
                common_sampler_accept(smpl, session_tokens[i], /* accept_grammar= */ false);
            }
            n_past = (int) session_tokens.size();
            LOG_INF("%s: loaded a session with prompt size of %d tokens\n", __func__, (int)session_tokens.size());
        }
        params.n_keep = (int) session_tokens.size(); //重置上下文时至少需保留的tokens
    }else{
        LOG_ERR("The prompt file must be provided");
        return -1;
    }
    duration = GetCurrentUS() - start;
    std::cout << "load prompt use time:" << duration / 1000 << std::endl;
    // session_tokens的长度不能超过上下文长度,可以在param中设置
    if ((int) session_tokens.size() > n_ctx - 4) {
        LOG_ERR("%s: prompt is too long (%d tokens, max %d)\n", __func__, (int) session_tokens.size(), n_ctx - 4);
        return -1;
    }

    while (true) {
        //获取用户输入
        if (params.input_prefix_bos) {
            LOG_DBG("adding input prefix BOS token\n");
            embd.push_back(llama_vocab_bos(vocab));
        }
        LOG_INF("\nuser:");
        std::string buffer;
        std::string line;
        bool another_line = true;
        do {
            another_line = console::readline(line, params.multiline_input); //可以在终端换行,也可以用其它方式获取用户输入
            buffer += line;
        } while (another_line);

        if (buffer.back() == '\n') {
            buffer.pop_back();
        }
        //处理输入数据
        if (!buffer.empty()) {
            if (params.escape) {
                string_process_escapes(buffer);
            }
            size_t pos;
            if((pos=buffer.find("-c"))!=std::string::npos){
                buffer.erase(pos, 2);
                buffer = "以下是指令控制模式:" + buffer;
                unit_mode = false;
            }else{
                buffer = "以下是知识问答:" + buffer;
                unit_mode = true;
            }
            std::string user_inp = chat_add_and_format("user", std::move(buffer));//此时user_inp会是<|im_start|>user “输入内容” <|im_end|> <|im_start|>assistant

            const auto line_pfx = common_tokenize(ctx, params.input_prefix, false, true);
            const auto line_inp = common_tokenize(ctx, user_inp,            false, true);
            const auto line_sfx = common_tokenize(ctx, params.input_suffix, false, true);

            // if (need_insert_eot) {
            //     llama_token eot = llama_vocab_eot(vocab);
            //     embd.push_back(eot == LLAMA_TOKEN_NULL ? llama_vocab_eos(vocab) : eot);
            //     need_insert_eot = false;
            // }

            embd.insert(embd.end(), line_pfx.begin(), line_pfx.end());
            embd.insert(embd.end(), line_inp.begin(), line_inp.end());
            embd.insert(embd.end(), line_sfx.begin(), line_sfx.end());
 
            for (size_t i = 0; i < embd.size(); ++i) {
                const llama_token token = embd[i];
                const std::string token_str = common_token_to_piece(ctx, token);
                common_sampler_accept(smpl, token, /* accept_grammar= */ false);
                //input_tokens.push_back(token);
                //output_ss << token_str;
            }
            assistant_ss.str("");
        }
        is_interacting = false;
        common_sampler_reset(smpl);
        if(!unit_mode){
            llama_memory_seq_rm(mem, 0, params.n_keep, -1); //从params.n_keep删到最后
            n_past = params.n_keep;
        }
        // 开始预测
        start = GetCurrentUS();
        while(!is_interacting){
            if (!embd.empty()) {
                int max_embd_size = n_ctx - 4;
                if ((int) embd.size() > max_embd_size) {
                    const int skipped_tokens = (int) embd.size() - max_embd_size;
                    embd.resize(max_embd_size);

                    LOG_WRN("<<input too long: skipped %d token%s>>", skipped_tokens, skipped_tokens != 1 ? "s" : "");
                }

                if (ga_n == 1 && unit_mode) {
                    //上下文长度超过之后的处理,从n_keep处开始到中途一半处开始删除,此功能仅会在知识问答模式使用
                    if (n_past + (int) embd.size() >= n_ctx) {
                        if (params.n_predict == -2) {
                            LOG_DBG("\n\n%s: context full and n_predict == -%d => stopping\n", __func__, params.n_predict);
                            break;
                        }
                    
                        const int n_left    = n_past - params.n_keep;
                        const int n_discard = n_left/2;

                        LOG_INF("context full, swapping: n_past = %d, n_left = %d, n_ctx = %d, n_keep = %d, n_discard = %d\n",
                                n_past, n_left, n_ctx, params.n_keep, n_discard);

                        llama_memory_seq_rm (mem, 0, params.n_keep            , params.n_keep + n_discard); //从bos删到接近n_past的一半
                        llama_memory_seq_add(mem, 0, params.n_keep + n_discard, n_past, -n_discard);
                        //LOG_INF("input_token's size is:%ld\n", input_tokens.size());
                        //LOG_INF("output_tokens's size is:%ld\n", output_tokens.size());
                        n_past -= n_discard;

                        LOG_INF("after swap: n_past = %d\n", n_past);
                        LOG_INF("embd: %s\n", string_from(ctx, embd).c_str());
                    }
                }
                
                //以n_batch为批次开始推理？和后面的embd_inp填充embd有点冲突
                for (int i = 0; i < (int) embd.size(); i += params.n_batch) {
                    int n_eval = (int) embd.size() - i;
                    if (n_eval > params.n_batch) {
                        n_eval = params.n_batch;
                    }
            
                    LOG_DBG("eval: %s\n", string_from(ctx, embd).c_str());
                    if (llama_decode(ctx, llama_batch_get_one(&embd[i], n_eval))) {
                        LOG_ERR("%s : failed to eval\n", __func__);
                        return 1;
                    }

                    n_past += n_eval;
                }
            }
            embd.clear();

            const llama_token id = common_sampler_sample(smpl, ctx, -1); //采样获得的令牌
            common_sampler_accept(smpl, id, /* accept_grammar= */ true); //如果接受采样获得的令牌,更新采样链等参数
            embd.push_back(id);
            //output_tokens.push_back(id);
            //output_ss << common_token_to_piece(ctx, id, params.special);
            LOG("%s", common_token_to_piece(ctx, id, params.special).c_str());//逐字符输出

                
            // 判断是否为结束token
            if (llama_vocab_is_eog(vocab, common_sampler_last(smpl))) {
                if (params.interactive) {
                    if (params.enable_chat_template) {
                        chat_add_and_format("assistant", assistant_ss.str());
                        if(!unit_mode){
                            param_json.pars_control(assistant_ss.str(), result, buffer);
                            std::cout << std::endl;
                            for (const auto& p : result) {
                                std::cout << "[param_name = " << p.name << "] ";
                                switch (p.value_type) {
                                    case TYPE_BOOL:
                                        std::cout << "bool_value = " << p.value.b;
                                        break;
                                    case TYPE_INT:
                                        std::cout << "int_value = " << p.value.i;
                                        break;
                                    case TYPE_FLOAT:
                                        std::cout << "float_value = " << p.value.f;
                                        break;
                                    case TYPE_STRING:
                                        std::cout << "str_value = " << p.value.s;
                                        break;
                                }
                                std::cout << std::endl;
                            }
                            duration = GetCurrentUS() - start;
                            std::cout << "use time:" << duration / 1000 << std::endl;
                            result.clear();
                        }
                    }
                    is_interacting = true;
                }
            }
            // 如果不是结束符,将token添加进assistant message中
            assistant_ss << common_token_to_piece(ctx, common_sampler_last(smpl), false);
        }
        //chat_msgs.clear();
    }
    //common_perf_print(ctx, smpl);
    common_sampler_free(smpl);
    llama_backend_free();
    // ggml_threadpool_free_fn(threadpool);
    //ggml_threadpool_free_fn(threadpool_batch);

    return 0;
}
