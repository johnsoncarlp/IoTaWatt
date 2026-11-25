#ifdef IOTAWATT_HOST_TEST
#include "host_config_deps.h"           // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#include "uploaders/Uploader_Registry.h"
#endif

bool configDevice(const char*);
bool configDST(const char* JsonStr);
bool configInputs(const char*);
void configPhaseShift();
bool configMasterPhaseArray();
bool configOutputs(const char*);
void hashFile(uint8_t* sha, File file);
bool exportLogConfig(const char *configObj);
bool configIntegrators(const char *);
bool configSimSolar(const char *);

//************************************************************************************************
//
// updateConfig(const char *newConfig)
//
// This procedure attempts to preserve the integrity of the config file by testing the new config
// and regressing if it fails.
//
// setConfig is called with the newConfig.
// if it fails,
//   setconfig is called with the pre-existing config and return false;
// if it succeeds,
//   The pre-existing config is renamed to //config-1.txt
//   The newConfig is renamed to config.txt
//   return true
// 
//*************************************************************************************************
bool updateConfig(const char *newConfig){
  if(setConfig(newConfig)){
    if(SD.exists(F(IOTA_CONFIG_OLD_PATH))){
      SD.remove(F(IOTA_CONFIG_OLD_PATH));
    }
    SDFS.rename(F(IOTA_CONFIG_PATH), F(IOTA_CONFIG_OLD_PATH));
    SDFS.rename(newConfig,F(IOTA_CONFIG_PATH));
    return true;
  }
  setConfig(IOTA_CONFIG_PATH);
  return false;
}

//************************************************************************************************
//
// recoverConfig(){
//
// Attempt to recover from failed setConfig
//
//*************************************************************************************************
bool recoverConfig(){
  if(SD.exists(F(IOTA_CONFIG_NEW_PATH)) && setConfig(IOTA_CONFIG_NEW_PATH)){
    log("using new config+1.");
    return updateConfig(IOTA_CONFIG_NEW_PATH);
  }
  if(SD.exists(F(IOTA_CONFIG_OLD_PATH)) && setConfig(IOTA_CONFIG_OLD_PATH)){
        // Keep the rejected config for the user to fix rather than deleting it.
    if(SD.exists(F(IOTA_CONFIG_BAD_PATH))) SD.remove(F(IOTA_CONFIG_BAD_PATH));
    SDFS.rename(F(IOTA_CONFIG_PATH), F(IOTA_CONFIG_BAD_PATH));
    SDFS.rename(F(IOTA_CONFIG_OLD_PATH), F(IOTA_CONFIG_PATH));
    log("reverting to config-1, rejected config kept as %s.", IOTA_CONFIG_BAD_PATH);
    return true;
  }
  return false;
}

//*************************************************************************************************
//
//    setConfig() will process the argued config file and return true/false=good/bad
//
//*************************************************************************************************

boolean setConfig(const char* configPath){
  JsonDocument Json;
        
  //************************************** Load and parse Json Config file ************************

  trace(T_CONFIG,0);
  File ConfigFile = SD.open(configPath, FILE_READ);
  if(!ConfigFile) {
    log("setConfig: %s open failed.", configPath);
    return false;
  }
  hashFile(configSHA256, ConfigFile);
  String configSummary = JsonSummary(ConfigFile, 1);
  DeserializationError parseError = deserializeJson(Json, configSummary);
  JsonObject Config = Json.as<JsonObject>();
  trace(T_CONFIG,4);
  if (parseError || Config.isNull()) {
    log("Config file parse failed.");
    return false;
  }
  
  //************************************** Process misc first level stuff **************************
  
  delete[] updateClass;
  updateClass = charstar(Config[F("update")] | "NONE");

  localTimeDiff = 60.0 * jFloat(Config[F("timezone")]);
    
  if( ! Config["logdays"].isNull()){ 
    log("Current log overide days: %d", Current_log.setDays(jInt<int>(Config[F("logdays")])));
  }      

  //************************************ Configure device ***************************

  trace(T_CONFIG,5);
  JsonArray deviceArray = Config[F("device")];
  if( ! deviceArray.isNull()){
    char* deviceStr = JsonDetail(ConfigFile, deviceArray);
    configDevice(deviceStr);
    delete[] deviceStr;
  }  

  //************************************ Configure DST rule *********************************

  trace(T_CONFIG,10);
  delete timezoneRule;
  timezoneRule = nullptr;
  JsonArray dstruleArray = Config[F("dstrule")];
  if( ! dstruleArray.isNull()){
    char* dstruleStr = JsonDetail(ConfigFile, dstruleArray);
    configDST(dstruleStr);
    delete[] dstruleStr;
  }  

  //************************************ Configure input channels ***************************

  trace(T_CONFIG,15);
  JsonArray inputsArray = Config[F("inputs")];
  if( ! inputsArray.isNull()){
    char* inputsStr = JsonDetail(ConfigFile, inputsArray);
    configInputs(inputsStr);
    delete[] inputsStr;
  }

  //************************************ Lookup phase shift in tables ***********************

  trace(T_CONFIG,20);
  configMasterPhaseArray();

  // ************************************ configure integrators *************************

  {      
    trace(T_CONFIG,25);
    JsonArray integratorsArray = Config[F("integrators")];
    char* integratorsStr;
    if( ! integratorsArray.isNull()){
      integratorsStr = JsonDetail(ConfigFile, integratorsArray);
    } else {
      integratorsStr = charstar("[]");
    }
    configIntegrators(integratorsStr);
    delete[] integratorsStr;
  }

  // ************************************ configure outputs *******************************

  {      
    trace(T_CONFIG,30);
    delete outputs;
    outputs = nullptr;
    trace(T_CONFIG,30,1);
    JsonArray outputsArray = Config[F("outputs")];
    char* outputsStr;
    if( ! outputsArray.isNull()){
      outputsStr = JsonDetail(ConfigFile, outputsArray);
    } else {
      outputsStr = charstar("[]");
    }
    trace(T_CONFIG,30,2);
    configOutputs(outputsStr);
    if( ! outputs){                           // parse failed: never leave outputs null (status/query/feed use it)
      outputs = new ScriptSet();
    }
    trace(T_CONFIG,30,3);
    delete[] outputsStr;
  }
  
    // *************************** configure Uploader based uploaders ***********************

  {
    trace(T_CONFIG,35);
    char **uploader_names = get_uploader_list();
    for (int i = 0; uploader_names[i]; i++){
      char uploader_name_lower[16];
      strcpy(uploader_name_lower, uploader_names[i]);
      for (int j = 0; uploader_names[i][j]; j++){
        uploader_name_lower[j] = tolower(uploader_names[i][j]);
      }
      JsonArray uploader_array = Config[uploader_name_lower];
      if( ! uploader_array.isNull()){
        trace(T_CONFIG,35,1);
        char* config_json = JsonDetail(ConfigFile, uploader_array);
        trace(T_CONFIG,35,2);
        Uploader * uploader = new_uploader(uploader_names[i]);
        trace(T_CONFIG,35,3);
        if(!uploader->config(config_json)){
          log("%s: Invalid configuration.", uploader_names[i]);
          uploader->end();
          delete_uploader(uploader_names[i]);
        }
        trace(T_CONFIG,35);
        delete[] config_json;
      }
      else{
        delete_uploader(uploader_names[i]);
      }
    }
  }

  
      // ************************ configure PVoutput (not Uploader based) ******************

  {
    trace(T_CONFIG,50);
    JsonArray PVoutputArray = Config[F("pvoutput")];
    if( ! PVoutputArray.isNull()){
      char* PVoutputStr = JsonDetail(ConfigFile, PVoutputArray);
      if(! PVoutput){
        PVoutput = new PVoutput_uploader();
      }
      if( ! PVoutput->config(PVoutputStr)){
        log("PVoutput: Invalid configuration."); 
      } 
      delete[] PVoutputStr;
    }   
    else if(PVoutput){
      PVoutput->end();
    }    
  }

      //***************************************** configure simsolar ***************************************

    trace(T_CONFIG,55);
    JsonArray simSolarArray = Config[F("simsolar")];
    if( ! simSolarArray.isNull()){
      char* simSolarStr = JsonDetail(ConfigFile, simSolarArray);
      configSimSolar(simSolarStr);
      delete[] simSolarStr;
    } 
    else {
      delete simsolar;
      simsolar = nullptr;
    }



  hashFile(configSHA256, ConfigFile);
  ConfigFile.close();
  trace(T_CONFIG,70);
  return true;

}  // End of setConfig

bool configSimSolar(const char* JsonStr){
  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonObject simConfig = Json.as<JsonObject>();
  if(parseError || simConfig.isNull()){
    log("simsolar: Json parse failed");
  }
  if( ! simsolar){
    simsolar = new simSolar;
  }
  uint32_t sunrise = jInt<int>(simConfig["sunrise"]);
  uint32_t sunset = jInt<int>(simConfig["sunset"]);
  uint32_t power = jInt<int>(simConfig["power"]);
  simsolar->config(sunrise, sunset, power);
  return true;
} 
//************************************** configDevice() ********************************************

bool configDevice(const char* JsonStr){

  hasRTC = true;
  VrefVolts = 2.5;
  ADC_selectPin[0] = pin_CS_ADC0;
  ADC_selectPin[1] = pin_CS_ADC1;

  int channels = 15; 
  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonObject device = Json.as<JsonObject>();
  if(parseError || device.isNull()){
    log("device: Json parse failed");
  }
  if( ! device[F("name")].isNull()){
    delete[] deviceName;
    deviceName = charstar(jStr(device[F("name")]));
  }
      
  if( ! device[F("refvolts")].isNull()){
    VrefVolts = jFloat(device[F("refvolts")]);
  } 
          
  trace(T_CONFIG,5);
  channels = MIN((jInt<unsigned int>(device[F("channels")]) | MAXINPUTS), MAXINPUTS);
  
            // If first time after restart
            // Instantiate the inputs

  if(maxInputs == 0) {
    inputChannel = new IotaInputChannel*[channels];
    for(int i=0; i<channels; i++){
      inputChannel[i] = new IotaInputChannel(i);
      String name = "Input(" + String(i) + ")";
      inputChannel[i]->_name = charstar(name);
    }
    maxInputs = channels;
  }

        // If channels has changed, restart.
        // Too many downsteam dependencies, safer to just restart.

  if(channels != maxInputs){
    log("Channels changing from %d to %d, restarting.", maxInputs, channels);
    delay(500);
    ESP.restart();
  }

        // Override all defaults with user specification
 
  if( ! device[F("chanaddr")].isNull()){
    for(int i=0; i<MIN(maxInputs,device[F("chanaddr")].size()); i++){
      unsigned int addr = jInt<unsigned int>(device[F("chanaddr")][i]);
      if(addr > 15){                          // 2 ADCs x 8 ports; addr >> 3 indexes ADC_selectPin[2]
        log("Config chanaddr[%d] = %u out of range, keeping %d.", i, addr, inputChannel[i]->_addr);
        continue;
      }
      inputChannel[i]->_addr = addr;
    }
  }
  
  if( ! device[F("chanaref")].isNull()){
    for(int i=0; i<MIN(maxInputs,device[F("chanaref")].size()); i++){
      inputChannel[i]->_aRef = jInt<unsigned int>(device[F("chanaref")][i]);
    }
  }
          // Get burden values.
          // If there is a burden file in the spiffs, use that.
          // else, if there is a burden object in the config, use that.
          // else, the default in the IotaInputChannel constructor prevails.

  const char burdenFileName[] = "/config/device/burden.txt";
  if(spiffsFileExists(burdenFileName)){
    String JsonBurden = spiffsRead(burdenFileName);
    JsonDocument burdenDoc;
    deserializeJson(burdenDoc, JsonBurden);
    JsonArray burden = burdenDoc.as<JsonArray>();
    for(int i=0; i<MIN(maxInputs,burden.size()); i++){
      inputChannel[i]->_burden = jFloat(burden[i]);
    }
  }
  else if( ! device[F("burden")].isNull()){
    String JsonBurden;
    serializeJson(device[F("burden")], JsonBurden);
    spiffsWrite(burdenFileName, JsonBurden);
    for(int i=0; i<MIN(maxInputs,device[F("burden")].size()); i++){
      inputChannel[i]->_burden = jFloat(device[F("burden")][i]);
    }
  }

  delete[] HTTPSproxy;
  HTTPSproxy = nullptr;
  if( ! device[F("httpsproxy")].isNull()){
    HTTPSproxy = charstar(jStr(device[F("httpsproxy")]));
  }
  return true;
}

//********************************** configure DST *********************************************

bool configDST(const char* JsonStr){
  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonVariant dstRule = Json.as<JsonVariant>();
  if(parseError){
    log("DST: Json parse failed");
    return false;
  }
  timezoneRule = new tzRule;
  timezoneRule->useUTC = jBool(dstRule[F("utc")]);
  timezoneRule->adjMinutes = jInt<int>(dstRule[F("adj")]);
  timezoneRule->begPeriod.month = jInt<int>(dstRule["begin"]["month"]);
  timezoneRule->begPeriod.weekday = jInt<int>(dstRule["begin"]["weekday"]);
  timezoneRule->begPeriod.instance = jInt<int>(dstRule["begin"]["instance"]);
  timezoneRule->begPeriod.time = jInt<int>(dstRule["begin"]["time"]);
  timezoneRule->endPeriod.month = jInt<int>(dstRule["end"]["month"]);
  timezoneRule->endPeriod.weekday = jInt<int>(dstRule["end"]["weekday"]);
  timezoneRule->endPeriod.instance = jInt<int>(dstRule["end"]["instance"]);
  timezoneRule->endPeriod.time = jInt<int>(dstRule["end"]["time"]);

  /******************************************************************************************************************************
   * 
   *    Activate this code to test a dst rule.  Will run through ten years at one minute intervals and
   *    log changes in offset between local and UTC.
   * 
    
  Serial.printf("timezoneRule: minutes %d, UTC %s\r\n", timezoneRule->adjMinutes, timezoneRule->useUTC ? "true" : "false");
  Serial.printf("  begin: month %d, weekday %d, instance %d, time %d\r\n", timezoneRule->begPeriod.month, 
                 timezoneRule->begPeriod.weekday, timezoneRule->begPeriod.instance, timezoneRule->begPeriod.time);
  Serial.printf("    end: month %d, weekday %d, instance %d, time %d\r\n", timezoneRule->endPeriod.month, 
                 timezoneRule->endPeriod.weekday, timezoneRule->endPeriod.instance, timezoneRule->endPeriod.time);

  uint32_t utime = 1451606400UL;   // 01/01/2016 00:00:00
  uint32_t ltime = utime;
  int32_t adj = 0;
  for(uint32_t days=1; days<=365*10; days++){
    for(uint32_t min=0; min<(60*24); min++){
      utime+=60;
      ltime = UTC2LocalTimeutime);
      if(ltime - utime != adj){
        adj = ltime - utime;
        Serial.printf("UTC %s, local %s, offset %f\r\n", datef(utime).c_str(), datef(ltime).c_str(), float(adj/3600.0));
      }
    }
    yield();
  }              
********************************************************************************************************************************/
  return true;
} 

//********************************** configInputs ***********************************************
bool configInputs(const char* JsonStr){
  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonVariant JsonInputs = Json.as<JsonVariant>();
  if(parseError){
    log("inputs: Json parse failed");
    return false;
  }

  for(int i=0; i<MIN(maxInputs,JsonInputs.size()); i++) {
    if(JsonInputs[i].is<JsonObject>()){
      JsonObject input = JsonInputs[i].as<JsonObject>();
      if(i != jInt<int>(input["channel"])){
        log("Config input channel mismatch: %d", i);
        continue;
      }
      delete[] inputChannel[i]->_name;
      inputChannel[i]->_name = charstar(jStr(input[F("name")]));
      delete[] inputChannel[i]->_model;
      inputChannel[i]->_model = charstar(jStr(input[F("model")]));
      // Fix name change that removed (USA) in table
      if(strcmp_P(inputChannel[i]->_model,PSTR("TDC DA-10-09(USA)")) == 0){
        inputChannel[i]->_model[12] = 0;
      }
      inputChannel[i]->_turns = jFloat(input[F("turns")]);
      inputChannel[i]->_calibration = jFloat(input[F("cal")]);
      if(inputChannel[i]->_turns && inputChannel[i]->_burden){
        inputChannel[i]->_calibration = inputChannel[i]->_turns / inputChannel[i]->_burden;
      }
      inputChannel[i]->_phase = jFloat(input[F("phase")]);
      inputChannel[i]->_vphase = jFloat(input[F("vphase")]);
      inputChannel[i]->_vchannel = ( ! input["vref"].isNull()) ? jInt<int>(input["vref"]) : 0;
      inputChannel[i]->active(true);
      String type = jStr(input[F("type")]);
      inputChannel[i]->_reverse = input[F("reverse")] | false;
      if(type == "VT") {
        inputChannel[i]->_type = channelTypeVoltage; 
        inputChannel[i]->_vchannel = i;
      }
      else if (type == "CT"){
        inputChannel[i]->_type = channelTypePower;
        inputChannel[i]->_vchannel = jInt<int>(input[F("vchan")]);
        inputChannel[i]->_signed = input[F("signed")] | false;
        inputChannel[i]->_double = input[F("double")] | false;
      }  
      else{
        log("unsupported input type: %s", type.c_str());
      }
      if( ! input[F("vmult")].isNull()){
        inputChannel[i]->_vmult = jFloat(input[F("vmult")]);
      } 
      else {
        inputChannel[i]->_vmult = 1.0;
      }
      if(inputChannel[i]->_double){
        inputChannel[i]->_vmult *= 2.0;
      }
      {
        int vref = (type == "CT") ? jInt<int>(input[F("vchan")]) : ( ! input["vref"].isNull()) ? jInt<int>(input["vref"]) : inputChannel[i]->_vchannel;
        if(type == "VT") vref = i;
        if(vref < 0 || vref >= maxInputs){    // _vchannel is a byte: check before it truncates
          log("Config input %d: voltage reference %d out of range, using 0.", i, vref);
          inputChannel[i]->_vchannel = 0;     // used as an index on every sample
        }
      }
      
    }
    else {
      inputChannel[i]->reset();
    }
  }
  return true;
}

//********************************** configIntegrators ***********************************************

bool configIntegrators(const char* JsonStr){

      // Create new ScriptSet from Json config

  JsonDocument Json;
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonArray integratorsArray = Json.as<JsonArray>();
  if(parseError || integratorsArray.isNull()){
    log("integrators: Json parse failed");
    return false;
  }

      // Make existing integrations null so integrations can't reference integrations.

  ScriptSet *oldIntegrations = integrations;
  integrations = new ScriptSet();

      // Move integrators for enduring integrations

  ScriptSet* newIntegrations = new ScriptSet(integratorsArray);
  Script *newScript = newIntegrations->first();
  while(newScript){
    Script *oldScript = oldIntegrations->first();
    while(oldScript){
      if(strcmp(newScript->name(), oldScript->name()) == 0){
        newScript->setParm(oldScript->getParm());
        oldScript->setParm(nullptr);
        break;
      }
      oldScript = oldScript->next();
    }
    newScript = newScript->next();
  }

      // Remove integrators from abandoned integrations

  Script *oldScript = oldIntegrations->first();
  while(oldScript){
    if(oldScript->getParm()){
      ((integrator*)oldScript->getParm())->end();
      oldScript->setParm(nullptr);
    }
    oldScript = oldScript->next();
  }

        // Create integrators for new integrations and call config for all.

  newScript = newIntegrations->first();
  while(newScript){
    integrator *newIntegrator = (integrator *)newScript->getParm();
    if(! newIntegrator){ 
      newIntegrator = new integrator();
      newScript->setParm((void*)newIntegrator);
    }
    if(! newIntegrator->config(newScript)){
      log("integrator: config failed %s", newScript->name());
    }
    newScript = newScript->next();
  }

  delete oldIntegrations;
  delete integrations;
  integrations = newIntegrations;
  return true;
} 

//********************************** configOutputs ***********************************************

bool configOutputs(const char* JsonStr){
  JsonDocument Json;
  trace(T_CONFIG,31);
  DeserializationError parseError = deserializeJson(Json, JsonStr);
  JsonArray outputsArray = Json.as<JsonArray>();
  trace(T_CONFIG,31,1);
  if(parseError || outputsArray.isNull()){
    log("outputs: Json parse failed");
    return false;
  }
  trace(T_CONFIG,31,2);
  outputs = new ScriptSet(outputsArray);
  trace(T_CONFIG,31,3);
  // outputs->sort([](Script* a, Script* b)->int{
  //   int res = strcmp(a->name(), b->name());
  //   Serial.printf("%s, %s, %d\r\n",a->getUnits(), b->getUnits(), res);
  //   return res;
  // });
  return true;
} 

/********************************** configmasterPhaseArray ********************************************
 * 
 * Phase shift can be specified as an array of shift value steps dependent on Voltage (VT), or
 * current (CT) as well as frequency (50Hz or 60Hz).  These values go into the calculation of
 * net phase shift along with shift attributable to derived reference and timing in the sampling
 * code.  The net is used to shift the voltage array prior to calculating real power.
 * 
 * This function will:
 * 
 * 1) Build a working table of each unique model configured.
 * 2) Search the table.txt file for each model and the existence of either dynamic phase array.
 * 3) allocate a single array to contain each of the unique arrays.
 * 4) Parse each of the unique model/Hz arrays and add to the master array, zero delimited.
 * 5) Set pointers in each iotaInputChannel to their corresponding dynamic phase arrays
 * 
 * **********************************************************************************************/

struct  modelTable {
  char*     model;
  int16_t   p50_pos;
  int16_t   p60_pos;
  int16_t*  p50_ptr;
  int16_t*  p60_ptr;
};

int arraySize(File tableFile){
    int commas = 0;
    char in = tableFile.read();
    while(tableFile.available() && in != ']'){
      if(in == ',') commas++;
      in = tableFile.read();
    }
    return commas + 2;
}

bool arrayCopy(File tableFile, int16_t** entry){
    char buf[100];
    if( ! tableFile.find('[')){
      return false;
    }
    int count = 0;
    int len = tableFile.readBytesUntil(']', buf, 100);
    buf[len] = 0;
    char* pos = buf;
    while(*pos != 0){
      float num = strtof(pos, &pos);
      **entry = (num * 100) + 0.5;
      *entry += 1;
      pos = strchr(pos, ',');
      if(pos == nullptr) break;
      pos++;
    }
    **entry = 0;
    *entry += 1;
    return true;
}

void dumpTable(modelTable* table, int count){
  Serial.println("initial");
  for (int n = 0; n < count; n++){
    Serial.printf("%4d %4d %8x %8x %s\n", table[n].p50_pos, table[n].p60_pos, table[n].p50_ptr, table[n].p60_ptr, table[n].model);
  }
}


bool configMasterPhaseArray(){

  // Allocate a table for unique models

  trace(T_CONFIG,20,1);
  modelTable table[maxInputs];
  int models = 0;
  for (int i = 0; i < maxInputs; i++){
    if (inputChannel[i]->isActive()){
      int t;
      for (t = 0; t < models; t++){
        if (strcmp(inputChannel[i]->_model, table[t].model) == 0){
          break;
        }
      }
      if (t == models){
        table[t].model = inputChannel[i]->_model;
        table[t].p50_pos = 0;
        table[t].p60_pos = 0;
        table[t].p50_ptr = nullptr;
        table[t].p60_ptr = nullptr;
        models++;
      }
    }
  }

  // Lookup models in table file.
  // Count total masterPhaseArray entries required

  trace(T_CONFIG, 20, 2);
  File tableFile;
  String tableFileURL = IOTA_TABLE_PATH;
  tableFile = SD.open(tableFileURL, FILE_READ);
  if(!tableFile) return false;

  trace(T_CONFIG, 20, 3);
  int totalEntries = 0;
  for(int t=0; t<models; t++){
    trace(T_CONFIG, 21);
    tableFile.seek(0);
    if(tableFile.find(table[t].model)){
      trace(T_CONFIG, 22);
      int pos = tableFile.position();
      if(tableFile.findUntil("\"p50\":", 6, "}", 1)){
        table[t].p50_pos = tableFile.position();
        totalEntries += arraySize(tableFile);
      }
      tableFile.seek(pos);
      if(tableFile.findUntil("\"p60\":", 6, "}", 1)){
        table[t].p60_pos = tableFile.position();
        totalEntries += arraySize(tableFile);
      }
    }
  }

  // allocate master array to contain all of the individual arrays.
  // Pointers will be set to subarrays in master;

  trace(T_CONFIG, 20, 4);
  delete[] masterPhaseArray;
  masterPhaseArray = new int16_t[totalEntries];
  int16_t* nextArray = masterPhaseArray;

        // Copy arrays into master and save pointers to them.

  trace(T_CONFIG, 20, 5);
  for(int t=0; t<models; t++){
    if(table[t].p50_pos){
      table[t].p50_ptr = nextArray;
      tableFile.seek(table[t].p50_pos);
      if( ! arrayCopy(tableFile, &nextArray)){
        table[t].p50_ptr = nullptr;
      }
    }
    if(table[t].p60_pos){
      table[t].p60_ptr = nextArray;
      tableFile.seek(table[t].p60_pos);
      if( ! arrayCopy(tableFile, &nextArray)){
        table[t].p60_ptr = nullptr;
      }
    }
  }

  // Set array pointers in inputs

  trace(T_CONFIG, 20, 6);
  for(int i=0; i<maxInputs; i++){
    IotaInputChannel* input = inputChannel[i];
    input->_p50 = nullptr;
    input->_p60 = nullptr;
    if(input->isActive()){
      for(int t=0; t<models; t++){
        if(strcmp(input->_model, table[t].model) == 0){
          input->_p50 = table[t].p50_ptr;
          input->_p60 = table[t].p60_ptr;
          break;
        }
      }
    }
  }
    
  // for(int t=0; t<models; t++){
  //   Serial.printf("%s p50pos %d, p60pos %d\n", table[t].model, table[t].p50_pos, table[t].p60_pos);
  //   int16_t* ptr = table[t].p50_ptr;
  //   if(ptr){
  //     Serial.print("  p50");
  //     while(*ptr){
  //       Serial.print(", ");
  //       Serial.print((float)*ptr / 100.0);
  //       ptr++;
  //     }
  //   }
    
  //   ptr = table[t].p60_ptr;
  //   if(ptr){
  //     Serial.print("  p60");
  //     while(*ptr){
  //       Serial.print(", ");
  //       Serial.print((float)*ptr / 100.0);
  //       ptr++;
  //     }
  //   }
  //   Serial.println();
  // }

  trace(T_CONFIG, 20, 7);
  tableFile.close();
  return true;
}