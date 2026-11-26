#ifndef influxBase_h
#define influxBase_h

#ifdef IOTAWATT_HOST_TEST
#include "host_uploader_deps.h"         // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#endif
#include "Uploader.h"

/*****************************************************************************************
 *  influxBase: what the InfluxDB v1 (influxDB_uploader) and v2 (influxDB2_uploader)
 *  uploaders have in common. Both post the same line protocol built from the same
 *  configuration ("measurement", "fieldkey", "tagset", "heap", "stop"); they differ in the
 *  write endpoint, the query for the last entry, and authentication.
 * **************************************************************************************/

class influxBase : public Uploader
{
    public:
        influxBase(uint8_t traceModule)
            :_tagSet(nullptr)
            ,_measurement(nullptr)
            ,_fieldKey(nullptr)
            ,_staticKeySet(false)
            ,_heap(false)
            ,_traceModule(traceModule)
            {}

        virtual ~influxBase(){
            delete[] _measurement;
            delete[] _fieldKey;
            delete _tagSet;
        }

    protected:

        struct influxTag {
            influxTag* next;
            char*      key;
            char*      value;
            influxTag()
                :next(nullptr)
                ,key(nullptr)
                ,value(nullptr)
                {}
            ~influxTag(){
                delete[] key;
                delete[] value;
                delete   next;
            }
        } *_tagSet;

        char *_measurement;
        char *_fieldKey;
        bool _staticKeySet;
        bool _heap;
        const uint8_t _traceModule;                         // T_influx1 or T_influx2

        uint32_t handle_write_s();                          // build the line protocol and post it
        virtual String writeEndpoint() = 0;                 // where handle_write_s posts
        bool configLineProtocol(JsonObject& config);        // the shared settings; sorts the outputs
        String varStr(const char *in, Script *script);      // $device, $name, $units substituted
};

#endif
