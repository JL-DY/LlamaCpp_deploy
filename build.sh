export CMAKE=/home/jl/cmake-3.10.2/cmake-3.10.2-Linux-x86_64/bin

# configure
PROGRAM_NAME=main.cpp
LLAMA_LIBS_DIR=/media/Work/jl/Package/llama.cpp/

USE_LLAMA_SRC=0
ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd ) # 返回该脚本的绝对路径

# build rockx
BUILD_DIR=${ROOT_PWD}/build

if [[  -d "${BUILD_DIR}" ]];then
  rm -rf "${BUILD_DIR}/"
fi

mkdir build
#make clean
cd build
cmake -DPROGRAM_NAME=${PROGRAM_NAME} -DLLAMA_LIBS_DIR=${LLAMA_LIBS_DIR} -DUSE_LLAMA_SRC=${USE_LLAMA_SRC} ..
make -j4
make install
cd ..

echo "make successful!"
