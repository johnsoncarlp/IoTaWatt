// unpackUpdate(): release files in the official format (header, FILE records, Ed25519 signature over the
// SHA-256 of the body), built and signed here with a test key.
#include <unity.h>
#include "host_release_deps.h"
bool unpackUpdate(String version);
#include "releaseFile.cpp"

static uint8_t testPriv[32], testPub[32];
struct RelFile { std::string name, data; };

static void put32(std::string& s, uint32_t v){ for(int i = 0; i < 4; i++) s += (char)(v >> (8 * i)); }
static std::string body(const char* version, const std::vector<RelFile>& files, const char* magic = "IotaWatt"){
  std::string b(magic, 8); b.append(version, 8);
  for(auto& f : files){
    b += "FILE"; put32(b, f.data.size());
    std::string name = f.name; name.resize(24, '\0'); b += name;
    b += f.data; b.append((8 - f.data.size() % 8) % 8, '\0');
  }
  return b;
}
static std::string sign(const std::string& b, const uint8_t* priv, const uint8_t* pub){
  uint8_t h[32]; SHA256 s; s.reset(); s.update(b.data(), b.size()); s.finalize(h, 32);
  uint8_t sig[64]; Ed25519::sign(sig, priv, pub, h, 32);
  return b + std::string((char*)sig, 64);
}
static void stage(const char* version, const std::string& blob){
  std::string path = std::string("/download/") + version + ".bin";
  auto d = SD.create(path.c_str()); d->writeAt(0, (const uint8_t*)blob.data(), blob.size()); d->size = blob.size();
}
static std::string read(const std::string& path){
  auto d = SD.data(path.c_str()); if(!d) return "<missing>";
  std::string s(d->size, 0); d->readAt(0, (uint8_t*)s.data(), d->size); return s;
}
static std::vector<RelFile> sampleFiles(){
  std::string fw; for(int i = 0; i < 70000; i++) fw += (char)(i * 31 + 7);     // not a multiple of 8 or 512
  return {{"INDEX.HTM", "<html>IoTaWatt</html>"}, {"TABLES.TXT", "{\"version\":\"0.0.9\"}"}, {"IOTAWATT.BIN", fw}};
}

void setUp(void){ SD.reset(); hostLog.clear(); memcpy(publicKey, testPub, 32); }
void tearDown(void){}

static void test_valid_release_unpacks(void){
  auto files = sampleFiles();
  stage("02_09_00", sign(body("02_09_00", files), testPriv, testPub));
  TEST_ASSERT_TRUE(unpackUpdate("02_09_00"));
  TEST_ASSERT_EQUAL_STRING(files[0].data.c_str(), read("/02_09_00/INDEX.HTM").c_str());
  TEST_ASSERT_EQUAL_STRING(files[1].data.c_str(), read("/02_09_00/TABLES.TXT").c_str());
  std::string bin = read("/02_09_00/IOTAWATT.BIN");
  TEST_ASSERT_EQUAL_UINT32(files[2].data.size() + 32, bin.size());       // firmware + 32-char MD5
  TEST_ASSERT_TRUE(bin.compare(0, files[2].data.size(), files[2].data) == 0);
  bool verified = false; for(auto& l : hostLog) verified |= l.find("signature verified") != std::string::npos;
  TEST_ASSERT_TRUE(verified);
}

static void test_rejects_wrong_key(void){
  uint8_t otherPriv[32], otherPub[32];
  for(int i = 0; i < 32; i++) otherPriv[i] = 200 - i;
  Ed25519::derivePublicKey(otherPub, otherPriv);
  stage("02_09_00", sign(body("02_09_00", sampleFiles()), otherPriv, otherPub));
  TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));
}

static void test_rejects_tampering(void){
  std::string good = sign(body("02_09_00", sampleFiles()), testPriv, testPub);
  size_t firmwareByte = good.size() - 64 - 1000;                        // inside IOTAWATT.BIN data
  size_t sigByte = good.size() - 10;
  for(size_t at : {firmwareByte, sigByte}){
    std::string bad = good; bad[at] ^= 0x40;
    SD.reset(); stage("02_09_00", bad);
    TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));
  }
}

static void test_rejects_bad_header(void){
  stage("02_09_00", sign(body("02_09_00", sampleFiles(), "IotaWaTT"), testPriv, testPub));
  TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));                          // magic
  SD.reset();
  stage("02_09_00", sign(body("02_08_99", sampleFiles()), testPriv, testPub));
  TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));                          // header version != requested
}

static void test_rejects_missing_file_or_firmware(void){
  TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));                          // nothing staged
  auto noFw = sampleFiles(); noFw.pop_back();
  stage("02_09_00", sign(body("02_09_00", noFw), testPriv, testPub));
  TEST_ASSERT_FALSE(unpackUpdate("02_09_00"));                          // signed, but no IOTAWATT.BIN
}

int main(int, char**){
  for(int i = 0; i < 32; i++) testPriv[i] = (uint8_t)(i * 7 + 3);
  Ed25519::derivePublicKey(testPub, testPriv);
  UNITY_BEGIN();
  RUN_TEST(test_valid_release_unpacks);
  RUN_TEST(test_rejects_wrong_key);
  RUN_TEST(test_rejects_tampering);
  RUN_TEST(test_rejects_bad_header);
  RUN_TEST(test_rejects_missing_file_or_firmware);
  return UNITY_END();
}
