# Setup for Linux Distributions (Ubuntu 24.04)

1. Install system packages.
2. Build & install **Abseil** (LTS 20240116.0, tests OFF).
3. Build & install **Protobuf v27.5** (shared, PIC, uses Abseil).
4. Clone NSB, replace top-level `CMakeLists.txt` (see below).
5. Configure, build, install NSB.
6. Set `PYTHONPATH` for proto stubs.
7. Start Redis on **port 5050**.
8. Run `nsb_daemon ../config.yaml`.

---

## 0. Assumptions

- Install prefix: `/usr/local`
- Build Abseil & Protobuf from source
- NSB repo lives at: `~/nsb_beta`
- Python output path (adjust if needed):  
  `/users/$username$/nsb_beta/build/generated/python`

---

## 1. Install System Packages

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libsqlite3-dev \
  libyaml-cpp-dev \
  libhiredis-dev \
  python3 \
  redis-server \
  git
   python3-pip
```
## 2. Build & Install Abseil (LTS 20240116.0)

```bash
cd ~
git clone --depth 1 --branch 20240116.0 https://github.com/abseil/abseil-cpp.git
cd abseil-cpp
mkdir build && cd build
cmake .. \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_BUILD_TYPE=Release \
  -DABSL_ENABLE_INSTALL=ON \
  -DBUILD_TESTING=OFF
cmake --build . --parallel
sudo cmake --install .
sudo ldconfig
```
#### Sanity Check
```bash
ls /usr/local/lib/libabsl_log* /usr/local/lib/libabsl_base* 2>/dev/null
```

## 3. Build & Install Protobuf v27.5

```bash
cd ~
wget https://github.com/protocolbuffers/protobuf/releases/download/v27.5/protobuf-27.5.tar.gz
tar -xvf protobuf-27.5.tar.gz
cd protobuf-27.5
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -Dprotobuf_BUILD_SHARED_LIBS=ON \
  -Dprotobuf_BUILD_TESTS=OFF \
  -Dprotobuf_ABSL_PROVIDER=package \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build . --parallel
sudo cmake --install .
sudo ldconfig
```

#### Sanity Check
```bash
which protoc
protoc --version      # Expect libprotoc 27.5
ls /usr/local/lib/libprotobuf.so*
```

## 4. Clone NSB
```bash
git clone https://github.com/nsb-ucsc/nsb_beta.git
cd nsb_beta
#Optional: git checkout linux-nsb
```

## 5.  Configure & Build NSB
```bash
rm -rf build && mkdir build && cd build
cmake -DProtobuf_PROTOC_EXECUTABLE=/usr/local/bin/protoc ..

cmake --build . --parallel
```

## 6. Install NSB (Optional but Recommended)
```bash
sudo cmake --install .
sudo ldconfig
```
Install locations:
Lib: /usr/local/nsb/lib/libnsb.so
Headers: /usr/local/nsb/include/...
Binary: /usr/local/nsb/bin/nsb_daemon
Python: /usr/local/nsb/bin/python_proto/

## 7 Set PYTHONPATH for Generated Python Proto
```bash
export PYTHONPATH=/users/nbhatia3/nsb_beta/build/generated/python:$PYTHONPATH
```
or ```bash
 cp -r build/generated/python/proto python/

#### To Persist
```bash
echo 'export PYTHONPATH=/users/nbhatia3/nsb_beta/build/generated/python:$PYTHONPATH' >> ~/.bashrc
source ~/.bashrc
```
or
```bash
cp -r build/generated/python/proto python/
```
#### Test:
```bash
python3 - <<'EOF'
import proto.nsb_pb2 as nsb_pb2
print("NSB Python proto loaded from:", nsb_pb2.__file__)
EOF
```
## 8. Start Redis on Port 5050
```bash
redis-server --port 5050 --daemonize yes
```
#### Verify: 
redis-cli -p 5050 ping  # Should return: PONG

## 9. Run NSB Daemon
```bash
cd ~/nsb_beta/build
./nsb_daemon ../config.yaml
```