cmake_minimum_required(VERSION 3.6)
project(llm)
set(CMAKE_CXX_STANDARD 17)

if(${USE_LLAMA_SRC})#这个暂时没用
    SET(LLM_DEPS ${LLAMA_LIBS_DIR}/build/libMNN.so #MNN的核心推理引擎库
                 ${LLAMA_LIBS_DIR}/build/express/libMNN_Express.so) #MNN的高级API封装库
                 
    include_directories(${CMAKE_CURRENT_LIST_DIR}/src/)
    include_directories(${CMAKE_CURRENT_LIST_DIR}/include/)
    include_directories(${LLAMA_LIBS_DIR}/3rd_party/
                        ${LLAMA_LIBS_DIR}/3rd_party/flatbuffers/include/
                        ${LLAMA_LIBS_DIR}/source/
                        ${LLAMA_LIBS_DIR}/tools/
                        ${LLAMA_LIBS_DIR}/schema/current/
                        ${LLAMA_LIBS_DIR}/include/)
    
    FILE(GLOB LLM_SRC ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
    add_executable(llm_demo ${CMAKE_CURRENT_LIST_DIR}/demo/${PROGRAM_NAME} ${LLM_SRC})
    target_link_libraries(llm_demo ${LLM_DEPS})
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/llm)
    install(TARGETS llm_demo DESTINATION ./)
    install(PROGRAMS ${LLM_DEPS} DESTINATION lib)
    set_target_properties(llm_demo PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
else()
    SET(LLM_DEPS ${LLAMA_LIBS_DIR}/build/common/libcommon.a
                 ${LLAMA_LIBS_DIR}/build/bin/libllama.so
                 ${LLAMA_LIBS_DIR}/build/bin/libggml.so
                 ${LLAMA_LIBS_DIR}/build/bin/libggml-base.so)

    include_directories(${CMAKE_CURRENT_LIST_DIR}/include/
                        ${LLAMA_LIBS_DIR}/include/
                        ${LLAMA_LIBS_DIR}/common/
                        ${LLAMA_LIBS_DIR}/ggml/include/)

    add_executable(llm_demo ${CMAKE_CURRENT_LIST_DIR}/demo/${PROGRAM_NAME})
    target_link_libraries(llm_demo ${LLM_DEPS} pthread curl)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/llm)
    install(TARGETS llm_demo DESTINATION ./)
    install(PROGRAMS ${LLM_DEPS} DESTINATION lib)
    set_target_properties(llm_demo PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
endif()
