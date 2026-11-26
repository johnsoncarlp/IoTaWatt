#ifndef influxDB2_uploader_h
#define influxDB2_uploader_h

#ifdef IOTAWATT_HOST_TEST
#include "host_uploader_deps.h"         // host unit tests (Firmware/test/shim)
#else
#include "IotaWatt.h"
#endif
#include "influxBase.h"

extern uint32_t influxDB_v2_dispatch(struct serviceBlock *serviceBlock);
extern Uploader*influxDB_v2;

class influxDB2_uploader : public influxBase
{
    public:
        influxDB2_uploader():
            influxBase(T_influx2),
            _lookbackHours(0),
            _orgID(0),
            _bucket(0),
            _token(0)
        {
            _id = charstar("influxDB2");
        };

        ~influxDB2_uploader(){
            delete[] _orgID;
            delete[] _bucket;
            delete[] _token;
            delete[] _id;
            delete _outputs;
            influxDB_v2 = nullptr;
        };

        bool configCB(const char *JsonText);
        uint32_t dispatch(struct serviceBlock *serviceBlock);

    private:

        uint32_t _lookbackHours;
        char *_orgID;
        char *_bucket;
        char *_token;

        uint32_t handle_query_s();
        uint32_t handle_checkQuery_s();
        uint32_t handle_checkWrite_s();
        String writeEndpoint();
        bool configCB(JsonObject &);

        void setRequestHeaders();
        int scriptCompare(Script *a, Script *b);
};

#endif