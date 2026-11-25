// unpackUpdate(): moved unchanged from updater.cpp so release-file handling can be unit tested on the
// host (test/test_release).
#ifdef IOTAWATT_HOST_TEST
#include "host_release_deps.h"          // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#endif

/**************************************************************************************************
 * bool unpackUpdate(String version)
 * 
 * unpack the release blob to create a directory of support files to be
 * installed during the next restart, as well as the firmware binary
 * file with md-5 appendage to be installed before restart.
 * 
 * After unpacking, the signature on the file is verified using the IoTaWatt public key.
 * Only release files from IotaWatt.com can be verified because the private-key is needed to
 * sign with the digital signature.
 * 
 *************************************************************************************************/

bool unpackUpdate(String version){

  union {
    struct {
      char file[4];
      uint32_t len;
      char name[24];
    } fileHeader;
    struct { 
      char IotaWatt[8];
      char release[8];
    } updtHeader;
    uint8_t header[32];
  } headers;

  bool binaryFound = false;
  SHA256 sha256;
  MD5Builder md5;

        // open the release file and setup to sha256

  String filePath = "download/" + version + ".bin";
  File releaseFile = SD.open((char*)filePath.c_str(), FILE_READ);
  if(! releaseFile){
    log("Updater: %s not found", filePath.c_str());
    return false;
  }
  int signatureSize = 64;
  uint32_t binarySize = releaseFile.size() - signatureSize;
  sha256.reset();

        // Read and verify the header

  if(true){
    releaseFile.read(headers.header,sizeof(headers.updtHeader));
    sha256.update(headers.header,sizeof(headers.updtHeader));
    binarySize -= sizeof(headers.updtHeader);
    if((memcmp(headers.updtHeader.IotaWatt, "IotaWatt", 8) != 0) ||
       (memcmp(headers.updtHeader.release, version.c_str(), 8) != 0)) {
      log("Updater: release file header invalid. %s %s",headers.updtHeader.IotaWatt,headers.updtHeader.release);
      releaseFile.close();
      return false;
    }
  }
        // Create the local update directory.

  deleteRecursive(String(version));
  if( ! SD.mkdir(version.c_str())){
    log("Updater: Cannot create update directory");
    releaseFile.close();
    return false;
  }
        // Read the update blob and create the various files.

  while(binarySize){
    releaseFile.read(headers.header,sizeof(headers.fileHeader));
    sha256.update(headers.header,sizeof(headers.fileHeader));
    binarySize -= sizeof(headers.fileHeader);
    if(memcmp(headers.fileHeader.file,"FILE",4) != 0) {
      log("Updater: Release file format error.");
      releaseFile.close();
      return false;
    }
    String filename = String(headers.fileHeader.name);
    bool iotawattBin = filename.equalsIgnoreCase("iotawatt.bin");
    if(iotawattBin){
      binaryFound = true;
      md5.begin();
    }
    String filePath = version + "/" + headers.fileHeader.name;
    uint32_t fileSize = headers.fileHeader.len;
    File outFile = SD.open((char*)filePath.c_str(), FILE_WRITE);
    if( ! outFile){
      log("Updater: unable to create file: %s", filePath.c_str());
      releaseFile.close();
      return false;
    }
    int buffSize = 512;
    uint8_t* buff = new uint8_t [buffSize];
    while(fileSize){
      int chunk = MIN(fileSize, buffSize);
      if(chunk % 8) chunk += 8 - chunk % 8;
      releaseFile.read(buff, chunk);
      sha256.update(buff, chunk);
      binarySize -= chunk;
      outFile.write(buff, MIN(fileSize, buffSize));
      if(iotawattBin) md5.add(buff, MIN(fileSize, buffSize));
      fileSize -= MIN(fileSize, buffSize);
    }
    delete[] buff;
    if(iotawattBin) {
      md5.calculate();
      uint8_t md5Char[32];
      md5.getChars((char*)md5Char);
      outFile.write(md5Char, 32);
    }
    outFile.close();
    if(fileSize){
      return false;
    } 
  }
        // verify the signature

  
  uint8_t signature[64];
  uint8_t sha[32];
  sha256.finalize(sha,32);
  if(releaseFile.available() != 64){
    log("Updater: Update rejected, no signature.");
    releaseFile.close();
    return false;
  }
  releaseFile.read(signature, 64);
  releaseFile.close();
  
  uint8_t* key = new uint8_t[32];
  memcpy_P(key, publicKey, 32);
  if(! Ed25519::verify(signature, key, sha, 32)){
    log("Updater: Signature does not verify.");
    delete[] key;
    return false;
  }
  delete[] key;
  log("Updater: signature verified");
  return binaryFound;
}
