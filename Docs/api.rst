=========================
HTTP API for integrations
=========================

This page is for people connecting other software to IoTaWatt Reimagined: Home Assistant, Node-RED,
Telegraf, scripts. It describes what the firmware promises to keep stable.

-----------------------------
Data plane and control plane
-----------------------------

The device's HTTP server has two kinds of endpoints:

**Data plane**: read-only access to the measurements and to the device's status. This is the API for
integrations, and the endpoints below are the complete list. They never return the configuration,
passwords, API keys or files.

**Control plane**: everything the web interface uses to configure and manage the device (the
configuration file, the file manager, passwords, commands, firmware updates). These endpoints are not
an integration API: they can change without notice. Use the web interface.

The two planes follow the device's two logins:

* **user**: the data plane. Give integrations a user login.
* **admin**: both planes.

Logins use HTTP Digest authentication (``qop=auth``). With no admin password set, there is no login
at all and both planes are open to anyone on the network; setting an admin password (Setup,
Passwords) is strongly recommended, and a separate user password for integrations keeps the admin
password out of them.

----------------------
Data plane endpoints
----------------------

All are ``GET`` requests. Responses are JSON unless stated otherwise.

/status
.......

Current state. Each section is included when its parameter is present (any value, e.g.
``/status?inputs&outputs`` or ``/status?wifi=yes``):

``stats``
    ``version`` (firmware, e.g. ``03_00_00``), ``product`` (``IoTaWatt Reimagined``), ``starttime``,
    ``currenttime``, ``runseconds``, ``stack`` (free memory, bytes), ``frequency`` (Hz),
    ``cyclerate``, ``chanrate``, ``lowbat`` (RTC battery low).
``inputs``
    An array of the active inputs: per input ``channel``; for a voltage input ``Vrms``, ``Hz`` and
    ``phase``; for a power input ``Watts`` (a string, whole watts), ``Pf``, ``phase``, ``lastphase``,
    and ``reversed`` when its CT is reversed.
``outputs``
    An array: per output ``name``, ``units``, ``value``.
``wifi``
    ``connecttime``, ``SSID``, ``IP``, ``channel``, ``RSSI``, ``mac``.
``datalogs``
    An array: per log (``Current``, ``History``, integrators) ``id``, ``firstkey``, ``lastkey``
    (UNIX times), ``size`` (bytes), ``interval`` (seconds).
``uploaders``
    An array: per uploader ``id``, ``status`` (``running`` or ``stopped``), ``lastpost`` (UNIX time),
    and ``message`` when there is one.
``device``
    ``name``, ``timediff`` (minutes from UTC), ``allowdst``, ``update``.
``passwords``
    Whether ``admin`` and ``user`` passwords are set, and ``localAccess``: flags only.

/query
......

Historical data from the data logs, as a table in JSON or CSV. The parameters are described in
:doc:`query` (``show=series`` lists what can be selected; ``select`` returns values). In this firmware
a query is ended after 60 seconds, so it can't stall the device: the response then ends early with
``"limit":<time>`` and ``"timeout":true`` (JSON with ``header=yes``) or with
``Limit exceeded at <time> (time limit)`` (CSV), and the client continues from that time. A JSON
query without a header can't carry that marker, so it is cut off instead (the client sees an error).

/feed/list.json and /feed/data.json
...................................

The Emoncms-compatible interface (used by Emoncms-style graph apps). ``list.json`` returns the feeds:
per input or output ``id`` (``IV``, ``IP`` or ``IE`` plus the name: voltage, power, energy), ``tag``,
``name``. ``data.json?id=<id>&start=<ms>&end=<ms>&interval=<s>`` (or ``mode=daily|weekly|monthly|
yearly``) returns ``[[time_ms, value], ...]``.

/graph/getallplus
.................

The graphs saved with Graph+: names and ids (``/graphs/<id>`` returns one).

---------------------
Errors and limits
-----------------

``400``
    A bad request; ``/query`` answers ``{"error":"..."}``.
``401``
    A login is needed (Digest challenge in ``WWW-Authenticate``).
``503``
    Busy: the device is low on memory, or this caller sent data requests faster than it can serve
    them. Wait for the ``Retry-After`` seconds and repeat. Each caller (IP address) may send 6 heavy
    requests (``/status``, ``/query``, ``/feed/*``, graph lists) back to back, then one more every
    0.4 seconds; other callers aren't affected.

-------------
Compatibility
-------------

The data plane stays compatible: fields are added, never renamed or removed, and these endpoints keep
their parameters. The firmware's host tests pin the requests Home Assistant makes (``/status?wifi``,
``/status?inputs&outputs``, and its ``/query`` forms). If an integration of yours depends on something
not listed here, open an issue so it can be added and kept.
