/*
 * IoTaWatt web UI: the menu bar shared by every page, and the colour theme.
 *
 * A page includes bootstrap.min.css, bootstrap.min.js and this file, and puts <nav id="iotanav"></nav>
 * where the bar goes. On the main page (index.htm) the Setup, Status and Tools entries call its own
 * functions; on the other pages they go to the main page, which opens the section named after the
 * '#' (e.g. "/#inputs"). Graph+, Files and the message log open in their own tab, from every page.
 *
 * The theme follows the system's light/dark setting (Bootstrap's data-bs-theme on <html>).
 */
(function () {
    "use strict";

    var dark = window.matchMedia ? window.matchMedia("(prefers-color-scheme: dark)") : null;
    function applyTheme() {
        document.documentElement.setAttribute("data-bs-theme", dark && dark.matches ? "dark" : "light");
    }
    applyTheme();
    if (dark && dark.addEventListener) dark.addEventListener("change", applyTheme);

    // Main page sections: key (the '#' in links), label, and index.htm function that opens it.
    var setup = [
        ["device", "Device", "configDevice"],
        ["inputs", "Inputs", "configInputs"],
        ["outputs", "Outputs", "configOutputs"],
        ["integrators", "Integrators", "configIntegrators"],
        ["uploaders", "Data uploaders", "buildWebServer"],
        ["proxy", "HTTPS proxy server", "setProxy"],
        ["passwords", "Passwords", "setPasswords"],
        ["burden", "Burden resistors", "configBurden"]
    ];
    var tools = [
        ["files", "Files", null, "edit.htm"],
        ["messages", "Message log", null, "iotawatt/iotamsgs.txt?textpos=-10000"],   // its last 10,000 bytes
        ["wifi", "Wi-Fi", "toolsWiFi"],
        ["restart", "Restart", "toolsRestart"]
    ];
    var sections = {};
    setup.concat(tools).concat([["status", "Status", "statusBegin"]]).forEach(function (s) {
        if (s[2]) sections[s[0]] = s[2];
    });

    function isMain() { return !!document.getElementById("mainBody"); }     // index.htm (known once the body is parsed)
    var page = location.pathname.replace(/^.*\//, "") || "index.htm";

    function el(tag, attrs, text) {
        var e = document.createElement(tag);
        for (var k in attrs) e.setAttribute(k, attrs[k]);
        if (text !== undefined) e.textContent = text;
        return e;
    }

    // A menu entry: a main page section, or a page of its own (opened in a new tab).
    function entry(s, cls) {
        var a;
        if (s[3]) {
            a = el("a", { "class": cls, href: s[3], target: "_blank", rel: "noopener" }, s[1]);
            if (page === s[3]) { a.classList.add("active"); a.removeAttribute("target"); }
        } else if (isMain()) {
            a = el("a", { "class": cls, href: "#" + s[0], "data-section": s[0] }, s[1]);
            a.addEventListener("click", function (e) {       // also when the '#' is already this one
                e.preventDefault();
                if (a.classList.contains("disabled")) return;
                history.replaceState(null, "", "#" + s[0]);
                open(s[0]);
            });
        } else {
            a = el("a", { "class": cls, href: "index.htm#" + s[0] }, s[1]);
        }
        return a;
    }

    function dropdown(label, items) {
        var li = el("li", { "class": "nav-item dropdown" });
        li.appendChild(el("a", { "class": "nav-link dropdown-toggle", href: "#", role: "button",
                                 "data-bs-toggle": "dropdown", "aria-expanded": "false" }, label));
        var ul = el("ul", { "class": "dropdown-menu" });
        items.forEach(function (s) {
            var item = el("li");
            item.appendChild(entry(s, "dropdown-item"));
            ul.appendChild(item);
        });
        li.appendChild(ul);
        return li;
    }

    function navItem(s) {
        var li = el("li", { "class": "nav-item" });
        li.appendChild(entry(s, "nav-link"));
        return li;
    }

    function build(nav) {
        nav.classList.add("navbar", "navbar-expand-md");   // add: a page may have its own (Graph+'s "embed")
        var box = el("div", { "class": "container-fluid" });
        var brand = el("a", { "class": "navbar-brand", href: "index.htm" });
        brand.appendChild(el("span", { "class": "iotanav-logo" }, "IoTaWatt"));
        brand.appendChild(el("span", { "class": "iotanav-edition" }, "Reimagined"));
        brand.appendChild(el("span", { id: "iotanavName", "class": "iotanav-name" }));
        box.appendChild(brand);
        var toggler = el("button", { "class": "navbar-toggler", type: "button", "data-bs-toggle": "collapse",
                                     "data-bs-target": "#iotanavMenu", "aria-controls": "iotanavMenu",
                                     "aria-expanded": "false", "aria-label": "Menu" });
        toggler.appendChild(el("span", { "class": "navbar-toggler-icon" }));
        box.appendChild(toggler);
        var menu = el("div", { "class": "collapse navbar-collapse", id: "iotanavMenu" });
        var ul = el("ul", { "class": "navbar-nav" });
        ul.appendChild(navItem(["status", "Status", "statusBegin"]));
        ul.appendChild(dropdown("Setup", setup));
        ul.appendChild(navItem(["graph", "Graph+", null, "graph2.htm"]));
        ul.appendChild(dropdown("Tools", tools));
        menu.appendChild(ul);
        box.appendChild(menu);
        nav.appendChild(box);

        // On a phone, close the menu after a choice.
        menu.addEventListener("click", function (e) {
            if (e.target.matches("a:not(.dropdown-toggle)") && menu.classList.contains("show") && window.bootstrap) {
                window.bootstrap.Collapse.getOrCreateInstance(menu).hide();
            }
        });
    }

    // Main page: open a section (the '#'; Status when there is none), once the configuration is loaded.
    var configReady = false;
    function open(key) {
        if (!isMain() || !configReady) return;
        var fn = sections[key] || sections.status;
        if (typeof window[fn] === "function" && typeof window.mainMenuButton === "function") {
            window.mainMenuButton(null, window[fn]);
        }
    }
    function route() { open(location.hash.replace(/^#/, "")); }

    window.iotanav = {
        // A firmware version as people read it: "03_00_00" -> "3.0.0", "03_00_d7" -> "3.0-d7".
        version: function (v) {
            var m = /^(\d\d)_(\d\d)_(\w\w)$/.exec(v || "");
            if (!m) return v || "";
            return +m[1] + "." + +m[2] + (/^\d\d$/.test(m[3]) ? "." + +m[3] : "-" + m[3]);
        },
        setName: function (name) {
            var n = document.getElementById("iotanavName");
            if (n) n.textContent = name || "";
            if (name) document.title = name + (isMain() ? "" : " · " + document.title.replace(/^.*· /, ""));
        },
        configLoaded: function () {                     // called after every config load: route only the first
            if (configReady) return;
            configReady = true;
            route();
        },
        // The main page couldn't load its configuration: its sections can't open (Files still can, to fix it).
        configFailed: function () {
            var links = document.querySelectorAll("#iotanav a[data-section]");
            for (var i = 0; i < links.length; i++) {
                if (links[i].getAttribute("data-section") === "restart") continue;
                links[i].classList.add("disabled");
                links[i].setAttribute("aria-disabled", "true");
            }
        },
        // The main page closed its section (cancel, save, ...): show Status rather than an empty page.
        home: function () {
            if (!configReady) return;
            setTimeout(function () {
                var body = document.getElementById("mainBody");
                if (body && body.style.display === "none") {
                    if (location.hash) history.replaceState(null, "", location.pathname + location.search);
                    open("status");
                }
            }, 0);
        }
    };
    window.addEventListener("hashchange", route);

    function start() {
        var nav = document.getElementById("iotanav");
        if (!nav) return;
        build(nav);
        if (!isMain()) {
            var x = new XMLHttpRequest();
            x.onload = function () {
                try { window.iotanav.setName(JSON.parse(x.responseText).device.name); } catch (e) { }
            };
            x.open("GET", "/status?device=yes");
            x.send();
        }
    }
    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start);
    else start();
})();
