//*********************************************************************************************
//                   Global variables
//*********************************************************************************************

var savedgraphs = []; // JWYT - graph definitions saved on IoTaWatt for selection
var series = []; // All of the series available, loaded from initial query
var feedlist = []; // Currently selected series+units
var yaxes = []; // array of descriptors for currently active yaxes
var response = {}; // raw response from last data query
var chart = null; // the uPlot chart on show

var loading = false; // semaphores used to control asynchronous data query
var reload = false;
var refreshTimer;
var embed = false; // embedded graph i.e. only displays graph placeholder

var path = ""; // used to call home
const queryParameters = new URLSearchParams(window.location.search);

//*********************************************************************************************
//                      Small DOM and request helpers (no jQuery)
//*********************************************************************************************

function byId(id) { return document.getElementById(id); }
function all(selector) { return Array.prototype.slice.call(document.querySelectorAll(selector)); }

// Show or hide elements: an element, or every element a selector matches.
function setShown(target, shown) {
  (typeof target == "string" ? all(target) : [target]).forEach(function(e) {
    e.style.display = shown ? "" : "none";
  });
}

// Text from the device or a configuration, made safe to put in HTML.
function esc(s) {
  return String(s).replace(/[&<>"']/g, function(c) {
    return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
  });
}

// One handler for every element matching selector, present now or added later.
function onEvent(event, selector, handler) {
  document.addEventListener(event, function(e) {
    var target = e.target.closest ? e.target.closest(selector) : null;
    if (target) handler.call(target, e);
  });
}

function fire(element, event) { element.dispatchEvent(new Event(event, { bubbles: true })); }

// An asynchronous request. body (a string) makes it a form POST. ok gets the reply text.
function request(method, url, body, ok, fail, done) {
  var x = new XMLHttpRequest();
  x.open(method, url, true);
  if (body != null) x.setRequestHeader("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
  x.onload = function() {
    if (x.status >= 200 && x.status < 300) { if (ok) ok(x.responseText); }
    else if (fail) fail(x);
    if (done) done();
  };
  x.onerror = function() { if (fail) fail(x); if (done) done(); };
  x.send(body);
}

// The same, for a JSON reply: ok gets the parsed value; a reply that isn't JSON counts as failed.
function requestJSON(method, url, body, ok, fail, done) {
  request(method, url, body, function(text) {
    var value;
    try { value = JSON.parse(text); } catch (e) { if (fail) fail(null); return; }
    if (ok) ok(value);
  }, fail, done);
}

function isNumeric(v) { return !isNaN(parseFloat(v)) && isFinite(v); }

//*********************************************************************************************
//                      Predefined reporting periods that can be selected
//*********************************************************************************************

var initialperiod = "today"; // Initial period on reset
var period = initialperiod; // Currently selected period

var periodTable = [
  { label: "custom dates", begin: "", end: "" },
  { label: "last 10 minutes", begin: "s-10m", end: "s" },
  { label: "last 30 minutes", begin: "s-30m", end: "s" },
  { label: "last 1 hour", begin: "s-1h", end: "s" },
  { label: "last 3 hours", begin: "s-3h", end: "s" },
  { label: "last 6 hours", begin: "s-6h", end: "s" },
  { label: "last 12 hours", begin: "s-12h", end: "s" },
  { label: "last 24 hours", begin: "s-24h", end: "s" },
  { label: "last 48 hours", begin: "s-48h", end: "s" },
  { label: "last 2 days to date", begin: "d-1d", end: "s" },
  { label: "last 3 days to date", begin: "d-2d", end: "s" },
  { label: "last 7 days to date", begin: "d-7d", end: "s" },
  { label: "this week to date", begin: "w", end: "s" },
  { label: "this month to date", begin: "M", end: "s" },
  { label: "this year to date", begin: "y", end: "s" },
  { label: "today", begin: "d", end: "s" },
  { label: "yesterday", begin: "d-1d", end: "d" },
  { label: "2 days ago", begin: "d-2d", end: "d-1d" },
  { label: "3 days ago", begin: "d-3d", end: "d-2d" },
  { label: "last week", begin: "w-1w", end: "w" },
  { label: "2 weeks ago", begin: "w-2w", end: "w-1w" },
  { label: "3 weeks ago", begin: "w-3w", end: "w-2w" },
  { label: "last 4 weeks", begin: "w-4w", end: "w" },
  { label: "last month", begin: "M-1M", end: "M" },
  { label: "2 months ago", begin: "M-2M", end: "M-1M" },
  { label: "3 months ago", begin: "M-3M", end: "M-2M" },
  { label: "Last 6 months", begin: "M-6M", end: "M" },
  { label: "Last 12 months", begin: "M-12M", end: "M" },
  { label: "last year", begin: "y-1y", end: "y" },
  { label: "2 years ago", begin: "y-2y", end: "y-1y" },
];

// A select's options, and the first one whose text contains want selected (then "changed", as the user would).
function fillSelect(select, options, want) {
  select.innerHTML = "";
  options.forEach(function(o) {
    var opt = document.createElement("option");
    opt.value = o.value;
    opt.textContent = o.text;
    select.appendChild(opt);
  });
  for (var i = 0; i < select.options.length; i++) {
    if (select.options[i].textContent.indexOf(want) >= 0) { select.selectedIndex = i; break; }
  }
  fire(select, "change");
}

function build_period_selector() {
  fillSelect(byId("select-period"), periodTable.map(function(p, t) { return { value: t, text: p.label }; }), period);
}

// Handle period select

byId("select-period").addEventListener("change", function() {
  periodIndex = this.value;
  period = periodTable[periodIndex].label;
  custom_dates = periodIndex == 0;
  refresh &= periodTable[periodIndex].end == "s";
  byId("refresh-text").textContent = refresh ? " Freeze" : " Refresh";
  query();
});

//*********************************************************************************************
//                      Supported units and related parameters
//*********************************************************************************************

var initialunit = "Watts"; // Initial unit on reset
var unit = initialunit; // Currently selected unit

var units = [
  { unit: "Volts", group: "V", label: "V", stat: " ", dp: 1, min: "", max: "" },
  { unit: "Watts", group: "P", label: "W", stat: "I", dp: 1, min: "", max: "" },
  { unit: "Wh", group: "P", label: "Wh", stat: "S", dp: 2, min: "", max: "" },
  { unit: "Amps", group: "P", label: "A", stat: "I", dp: 3, min: "", max: "" },
  { unit: "VA", group: "P", label: "VA", stat: "I", dp: 1, min: "", max: "" },
  { unit: "PF", group: "P", label: "PF", stat: " ", dp: 3, min: "", max: "" },
  { unit: "Hz", group: "V", label: "Hz", stat: " ", dp: 2, min: "", max: "" },
  { unit: "VAR", group: "P", label: "VAR", stat: "I", dp: 1, min: "", max: "" },
  { unit: "VARh", group: "P", label: "VARh", stat: "S", dp: 2, min: "", max: "" },
];

// A colour from the page theme (light or dark), for the chart

function themeColor(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || null;
}

// Build unit select buttons

function build_units_selector() {
  var html = "";
  for (var u in units) {
    html += "<button class='btn btn-sm btn-outline-primary unit-buttons" + (units[u].unit == unit ? " active" : "") +
      "'>" + units[u].unit + "</button>";
  }
  byId("units-select").innerHTML = html;
}

function unitindex(unit) {
  for (var u in units) {
    if (units[u].unit == unit) return u;
  }
}

// Handle unit select button click

onEvent("click", ".unit-buttons", function() {
  all(".unit-buttons").forEach(function(b) { b.classList.remove("active"); });
  this.classList.add("active");
  unit = this.textContent;
  build_source_list();
});

//*********************************************************************************************
//                      Supported groups
//*********************************************************************************************

var initialgroup = "auto"; // Initial group on reset
var group = initialgroup; // Currently selected group

var groups = [
  { group: "auto", query: "auto" },
  { group: "Hourly", query: "1h" },
  { group: "Daily", query: "1d" },
  { group: "Weekly", query: "1w" },
  { group: "Monthly", query: "1M" },
  { group: "Yearly", query: "1y" },
  { group: "all", query: "all" },
];

function build_group_options() {
  fillSelect(byId("select-group"), groups.map(function(g) { return { value: g.group, text: g.group }; }), group);
}

byId("select-group").addEventListener("change", function() {
  group = this.value;
  query();
});

//*********************************************************************************************
//                      Colors that will be assigned to each plot series - in order of assignment
//*********************************************************************************************

var colors = [
  "#3a87fe", // med blue
  "#96d35f", // lt green
  "#e63b7a", // pottery red
  "#feb43f", // med orange
  "#be38f3", // violet
  "#01c7fc", // lt blue
  "#ff6250", // red orange
  "#e4ef65", // yellow green
  "#4e7a27", // dark green
  "#99244f", // fuchia
  "#fecb3e", // lt orange
  "#5e30eb", // dark blue
  "#ff8648", // orange
];

//*********************************************************************************************
//                      Table used to scale kilo, milli, etc with associated decimal precision
//*********************************************************************************************

var formats = [
  { max: 1, div: 0.001, prefix: "m", dp: 0 },
  { max: 10, div: 1, prefix: "", dp: 2 },
  { max: 100, div: 1, prefix: "", dp: 1 },
  { max: 1000, div: 1, prefix: "", dp: 0 },
  { max: 10000, div: 1000, prefix: "k", dp: 2 },
  { max: 100000, div: 1000, prefix: "k", dp: 1 },
  { max: 1000000, div: 1000, prefix: "k", dp: 0 },
  { max: 10000000, div: 1000000, prefix: "M", dp: 2 },
  { max: 100000000, div: 1000000, prefix: "M", dp: 1 },
  { max: 100000000000, div: 1000000, prefix: "M", dp: 0 },
];

//*********************************************************************************************
//                      context defines the current graph
//*********************************************************************************************

var userDateChange = true; // Date change would be by user
var custom_dates = false; // Period is user modified dates
var periodIndex = 0; // Index of selected period option
var periodDuration = 0; // Duration of graph period in seconds
var interval = 60; // Seconds represented by first group of response
var showLegend = true; // Show the graph legend
var showUnit = true; // Prefix series with unit: in graph legend
var refresh = false;

//********************************************************************************************
//        Graph Reset - make a fresh start
//********************************************************************************************

byId("graph-reset").addEventListener("click", function() {
  feedlist = [];
  response.data = [];
  byId("graph-name").value = "";
  setShown(byId("graph-save"), false);
  setShown(byId("graph-delete"), false);
  setShown(".reset-hide", false);
  byId("refresh-text").textContent = " Refresh";

  period = initialperiod;
  build_period_selector();
  unit = initialunit;
  build_units_selector();
  group = initialgroup;
  build_group_options();
  refresh = false;
  set_refresh();

  build_source_list();

  for (var u in units) {
    units[u].min = "";
    units[u].max = "";
  }
  byId("showSourceOptions").click();
});

//********************************************************************************************
//        unitFormat - returns display string of val+units w/appropriate scaling and dp
//                     example 2406 Watts becomes "2.41 kW"
//********************************************************************************************

function unitFormat(val, unit) {
  if (unit == "Volts") {
    return Number(val).toFixed(1) + " V";
  }
  if (unit == "PF") {
    if (val > 0.999) {
      return Number(val).toFixed(2) + " PF";
    }
    return (Number(val).toFixed(3) + " PF").substring(1);
  }
  if (unit == "Hz") {
    return Number(val).toFixed(2) + " Hz";
  }
  for (var u in units) {
    if (units[u].unit == unit) {
      if (val == 0) {
        return "0 " + units[u].label;
      }
      for (var f in formats) {
        if (Math.abs(val) < formats[f].max) {
          return (Number(val) / formats[f].div).toFixed(formats[f].dp) + " " + formats[f].prefix + units[u].label;
        }
      }
    }
  }
  return Number(val).toFixed(0) + " " + unit;
}

//********************************************************************************************
//        begin and end datetimepickers
//        Define the pickers, maintain min and max dates on change,
//        indicate custom period and graph-reload on change by user (explicit or implicit)
//********************************************************************************************

// The begin and end fields are the browser's own date-time inputs. DateInput keeps the exact time
// (to the second, as the server returns it) and offers the few calls of the old DateTimePicker that
// this page uses: date(), minDate(), maxDate() and viewDate(), whose value has format("x") (ms),
// diff(other, "seconds") and toJSON() (saved graphs store it as an ISO string).

function TimeValue(d) { this.d = d; }
TimeValue.prototype.format = function(f) { return f == "x" ? String(this.d.getTime()) : formatDate(this.d, f); };
TimeValue.prototype.diff = function(other) { return Math.round((this.d - other.d) / 1000); };
TimeValue.prototype.toJSON = function() { return this.d.toJSON(); };

function DateInput(id) {
  var input = document.getElementById(id);
  var current = new Date();
  function local(d) {
    return formatDate(d, "YYYY-MM-DD") + "T" + formatDate(d, "HH:mm");
  }
  this.input = input;
  this.date = function(d) { current = new Date(d); input.value = local(current); };
  this.minDate = function(d) { input.min = local(new Date(d)); };
  this.maxDate = function(d) { input.max = local(new Date(d)); };
  this.viewDate = function() { return new TimeValue(current); };
  this.fromInput = function() {
    var d = new Date(input.value);                        // "YYYY-MM-DDTHH:mm" is local time
    if (isNaN(d)) return false;
    current = d;
    return true;
  };
}

// "YYYY-MM-DD", "HH:mm", "YYYY-MM-DD HH:mm:ss", "YYYY-MM-DD_HHmm": the formats this page uses
function formatDate(d, f) {
  function p(n) { return n < 10 ? "0" + n : "" + n; }
  return f.replace("YYYY", d.getFullYear()).replace("MM", p(d.getMonth() + 1)).replace("DD", p(d.getDate()))
    .replace("HH", p(d.getHours())).replace("mm", p(d.getMinutes())).replace("ss", p(d.getSeconds()));
}

var beginDate = new DateInput("request-start");
var endDate = new DateInput("request-end");

function showError(text) {
  var e = byId("error");
  e.textContent = text;
  setShown(e, true);
}

// A date the user changed: a custom period, graphed once the field is left alone for a moment.
var dateTimer = null;
function dateChanged(which, other, setLimit) {
  if (!which.fromInput()) return;
  setLimit.call(other, which.viewDate().d);
  // A typed date isn't held to min/max like a picked one: don't query a reversed range.
  if (beginDate.viewDate().d >= endDate.viewDate().d) {
    clearTimeout(dateTimer);
    showError("The start must be before the end.");
    return;
  }
  setShown(byId("error"), false);
  custom_dates = true;
  clearTimeout(dateTimer);
  dateTimer = setTimeout(query, 600);
}
beginDate.input.addEventListener("change", function() { dateChanged(beginDate, endDate, endDate.minDate); });
endDate.input.addEventListener("change", function() { dateChanged(endDate, beginDate, beginDate.maxDate); });

function set_custom_dates(begin, end) {
  userDateChange = false;
  beginDate.maxDate(new Date(end * 1000));
  beginDate.date(new Date(begin * 1000));
  endDate.minDate(new Date(begin * 1000));
  endDate.date(new Date(end * 1000));
  userDateChange = true;
}

//********************************************************************************************
//        Zoom, Pan, Reload, legend
//********************************************************************************************

all(".zoom").forEach(function(b) {
  b.addEventListener("click", function() {
    custom_dates = true;
    var time_adj = round2group((periodDuration * (1 - this.value)) / 2);
    set_custom_dates(response.range[0] + time_adj, response.range[1] - time_adj);
    query();
  });
});

all(".pan").forEach(function(b) {
  b.addEventListener("click", function() {
    custom_dates = true;
    var time_adj = round2group(periodDuration * this.value);
    set_custom_dates(response.range[0] + time_adj, response.range[1] + time_adj);
    query();
  });
});

function set_refresh() {
  if (periodTable[periodIndex].end == "s" && refresh) {
    refreshTimer = setTimeout(function() {
      query();
    }, interval * 1000);
    byId("refresh-text").textContent = " Freeze";
  } else {
    byId("refresh-text").textContent = " Refresh";
  }
}

byId("refresh").addEventListener("click", function() {
  if (refresh) {
    clearTimeout(refreshTimer);
    byId("refresh-text").textContent = " Refresh";
    refresh = false;
  } else {
    refresh = true;
    query();
  }
});

function round2group(time) {
  var round = 24 * 3600;
  if (group == "hour") round = 3600;
  else if (group == "auto") round = 5;
  return time - (time % round);
}

byId("show-legend").addEventListener("click", function() {
  showLegend = !showLegend;
  this.textContent = showLegend ? "Hide Legend" : "Show Legend";
  graph();
});

//********************************************************************************************
//        A range picked on the chart (drag across it): graph that range
//********************************************************************************************

function rangeSelected(from, to) {
  custom_dates = true;
  set_custom_dates(from - (from % interval), to + interval - (to % interval));
  query();
}

//********************************************************************************************
//        Handle window and graph resize
//********************************************************************************************

var resizeTimer = null;
window.addEventListener("resize", function() {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(function() {
    if (chart) chart.setSize(chartSize());
  }, 100);
});

// The chart's plotting area: the full width, half as high (as before), within the window.
function chartSize() {
  var width = byId("placeholder_bound").clientWidth || 600;
  var height = Math.round(Math.min(width * 0.5, window.innerHeight * 0.75));
  return { width: width, height: Math.max(height, 200) };
}

function graph_resize() {
  if (chart) chart.setSize(chartSize());
}

//********************************************************************************************
//        CSV time-format, null-values, copy to clipboard, download
//********************************************************************************************

byId("csvtimeformat").addEventListener("change", function() { build_CSV(); });
byId("csvnullvalues").addEventListener("change", function() { build_CSV(); });

byId("copycsv").addEventListener("click", function() {
  var textArea = byId("csv");
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(textArea.value);
  } else {
    textArea.select();
    document.execCommand("Copy");
  }
});

byId("downloadcsv").addEventListener("click", function() {
  var element = document.createElement("a");
  element.setAttribute("href", "data:text/csv;charset=utf-8," + encodeURIComponent(byId("csv").value));
  element.setAttribute("download", "iotawatt_" + formatDate(new Date(), "YYYY-MM-DD_HHmm") + ".csv");
  element.style.display = "none";
  document.body.appendChild(element);
  element.click();
  document.body.removeChild(element);
});

//********************************************************************************************
//        Detail Lines - color, type(line/bar), fill, stack, decimals, scale
//********************************************************************************************

function feedOf(element) { return feedlist[element.getAttribute("feedindex")]; }

onEvent("change", ".linecolor", function() { feedOf(this).color = this.value; graph(); });

onEvent("click", ".line-bar", function() {
  this.textContent = this.textContent == "Line" ? "Bar" : "Line";
  feedOf(this).plottype = this.textContent;
  graph();
});

onEvent("change", ".fill", function() { feedOf(this).fill = this.checked; graph(); });
onEvent("change", ".stack", function() { feedOf(this).stack = this.checked; graph(); });
onEvent("change", ".accrue", function() { feedOf(this).accrue = this.checked; graph(); });
onEvent("change", ".decimalpoints", function() { feedOf(this).dp = this.value; query(); });
onEvent("change", ".scale", function() { feedOf(this).scale = this.value; graph(); });

//********************************************************************************************
//        Yaxes min and max
//********************************************************************************************

function yLimitChanged(input, which) {
  if (input.value == "auto") input.value = "";
  var val = input.value;
  if (val == "" || isNumeric(val)) {
    units[input.getAttribute("unitindex")][which] = val;
  }
  graph();
}
onEvent("change", ".ymin", function() { yLimitChanged(this, "min"); });
onEvent("change", ".ymax", function() { yLimitChanged(this, "max"); });

//********************************************************************************************
//        Detail Table select - CSV/Options/Statistics,Yaxis
//********************************************************************************************

setShown(".data-tables", false);
setShown(".data-table", false);
all(".show-tables").forEach(function(b) {
  b.addEventListener("click", function() {
    all(".show-tables").forEach(function(x) { x.classList.remove("active"); });
    this.classList.add("active");
    setShown(".data-table", false);
    var table = { stats: "sourceStatsTable", options: "sourceOptionsTable", yaxes: "yaxesTable", CSV: "CSVgroup" }[this.value];
    if (table) setShown(byId(table), true);
  });
});

byId("showSourceOptions").click();

//********************************************************************************************
//        re-arrange detail table entries
//********************************************************************************************

onEvent("click", ".move-feed", function() {
  var curpos = parseInt(this.getAttribute("feedid"));
  var newpos = curpos + parseInt(this.getAttribute("moveby"));
  if (newpos >= 0 && newpos < feedlist.length) {
    arrayMove(feedlist, curpos, newpos);
    graph();
  }
});

function arrayMove(array, old_index, new_index) {
  array.splice(new_index, 0, array.splice(old_index, 1)[0]);
  return array;
}

//********************************************************************************************
//        Handle series select
//********************************************************************************************

onEvent("click", "#source-table .source-table-entry", function() {
  var name = this.textContent;
  for (var z in feedlist) {
    if (feedlist[z].unit == unit && feedlist[z].name == name) {
      if (this.nextElementSibling) this.nextElementSibling.click();
      return;
    }
  }
  feedlist.push({
    name: name,
    unit: unit,
    color: assign_color(),
    fill: false,
    stack: false,
    scale: 1,
    accrue: false,
    dp: units[unitindex(unit)].dp,
    plottype: "Line",
  });
  build_source_list();
  query();
});

//********************************************************************************************
//        Handle delete feed
//********************************************************************************************

onEvent("click", ".feed-delete", function() {
  feedlist.splice(this.getAttribute("feedindex"), 1);
  graph();
});

//********************************************************************************************
//        Create source list table for the selected unit
//********************************************************************************************

function build_source_list() {
  var html = "<table style='width:100%'>";
  html += "<colgroup><col span='1' style='width: 90%;'><col span='1' style='width: 10%;'></colgroup>";
  for (var z in series) {
    if (source_type(unit) == source_type(series[z].unit)) {
      html += "<tr class='source-table-row'><td class='source-table-entry'>" + esc(series[z].name) + "</td>";
      var selected = false;
      for (var f in feedlist) {
        if (unit == feedlist[f].unit && series[z].name == feedlist[f].name) {
          html += "<td class='feed-delete' feedindex=" + f + " style='background:" + esc(feedlist[f].color) +
            "'><i class='ico ico-trash3'></i></td>";
          selected = true;
        }
      }
      if (!selected) {
        html += "<td></td>";
      }
      html += "</tr>";
    }
  }
  html += "</table>";
  byId("source-table").innerHTML = html;
}

function source_type(unit) {
  if (unit == "Volts" || unit == "Hz") return "voltage";
  return "power";
}

function assign_color() {
  if (feedlist.length == 0) return colors[0];
  for (var i in colors) {
    var result = colors[i];
    for (var z in feedlist) {
      if (result == feedlist[z].color) {
        result = false;
        break;
      }
    }
    if (result) return result;
  }
  return "#000000";
}

//********************************************************************************************
//            query() - initiate asynchronous query for all graph data
//
//            Process the feedlist and initiate a query.
//            If there is a query in progress (loading:true), the request is delayed (reload:true)
//            and processed on completion.  There are no parameters, only the current feedlist.
//********************************************************************************************

function query() {
  clearTimeout(refreshTimer);

  // This is the first part of the asynchronous load logic.
  // It's a basic semaphore that queues a request for later.
  // If there is a load in progress, set reload = true to trigger another load when the current one completes.

  if (loading) {
    reload = true;
    return;
  }

  // Not currently loading, set loading semaphore and proceed

  loading = true;

  // Build the query

  var begin = periodTable[periodIndex].begin;
  var end = periodTable[periodIndex].end;
  if (custom_dates) {
    begin = beginDate.viewDate().format("x");
    end = endDate.viewDate().format("x");
    byId("select-period").selectedIndex = 0;
  }

  var url = path + "/query?format=json&header=yes&resolution=high&missing=null" +
    "&begin=" + begin + "&end=" + end + "&select=[time.utc.unix";

  for (var i = 0; i < feedlist.length; i++) {
    url += "," + feedlist[i].name + "." + feedlist[i].unit + ".d" + feedlist[i].dp;
    feedlist[i].dataindex = i + 1;
  }

  url += "]&group=";
  for (var g in groups) {
    if (group == groups[g].group) {
      url += groups[g].query;
    }
  }

  // Send the query

  request("GET", url, null,
    function(data_in) {
      // Process query response
      var valid = true;
      try {
        response = JSON.parse(data_in);
        if (response.success != undefined) valid = false;
      } catch (e) {
        valid = false;
      }
      if (!valid) {
        byId("error").innerHTML = "<div class='alert alert-danger'><b>Request error</b> " + esc(data_in) + "</div>";
        setShown(byId("error"), true);
        return;
      }
      userDateChange = false;
      beginDate.maxDate(new Date(response.range[1] * 1000));
      beginDate.date(new Date(response.range[0] * 1000));
      endDate.minDate(new Date(response.range[0] * 1000));
      endDate.date(new Date(response.range[1] * 1000));
      userDateChange = true;
      periodDuration = endDate.viewDate().diff(beginDate.viewDate(), "seconds");
      if (response.data.length < 2) {
        interval = periodDuration;
      } else {
        interval = response.data[1][0] - response.data[0][0];
      }
      var autolabel = "auto";
      if (group == "auto") {
        if (interval % 3600 == 0) autolabel += " (" + interval / 3600 + "h)";
        else if (interval % 60 == 0) autolabel += " (" + interval / 60 + "m)";
        else autolabel += " (" + interval + "s)";
      }
      byId("select-group").options[0].textContent = autolabel;
      setShown(byId("error"), false);
      if (response.limit) {                            // cut short by the row limit or the time limit (B87)
        showError("Data shown up to " + new Date(response.limit * 1000).toLocaleString() +
          (response.timeout ? ": the device's time limit for one query was reached." : ": the row limit was reached.") +
          " Zoom in, or choose a coarser group.");
      }
      graph();
    },
    function(xhr) {
      byId("error").innerHTML = "<h4>" + esc(xhr.status + " " + xhr.statusText) + "</h4><p>" +
        esc(xhr.responseText) + "<br>" + esc(url.substring(0, 400)) + "</p>";
      setShown(byId("error"), true);
    },
    function() {
      // This is the second part of the asynchronous load logic.
      // Change state to indicate load no longer in progress.
      // If a reload was requested during this load, start a new load.
      clearTimeout(refreshTimer);
      loading = false;
      if (reload) {
        reload = false;
        query();
      } else {
        set_refresh();
      }
    });
}

//********************************************************************************************
//            graph() - Create a graph (uPlot)
//
//            One y axis per unit, alternating left and right. Each feed is a line or bars, filled
//            or not; stacked feeds sit on the stacked feed after them in the list (the last one
//            is at the bottom), on the same axis. Gaps (no data) are bridged by lines, as before.
//            Dragging across the chart graphs that range.
//********************************************************************************************

function hexAlpha(color, alpha) {                   // "#rrggbb" + alpha (0..1) -> "#rrggbbaa"
  if (!/^#[0-9a-f]{6}$/i.test(color)) return color;
  return color + ("0" + Math.round(alpha * 255).toString(16)).slice(-2);
}

function build_yaxes() {
  yaxes = [];
  var position = "left";
  for (var z in feedlist) {
    var y;
    for (y = 0; y < yaxes.length; ++y) {
      if (feedlist[z].unit == yaxes[y].unit) {
        feedlist[z].yaxis = y + 1;
        break;
      }
    }
    if (y == yaxes.length) {
      for (var u in units) {
        if (feedlist[z].unit == units[u].unit) {
          yaxes.push(units[u]);
          yaxes[yaxes.length - 1].unitIndex = u;
          yaxes[yaxes.length - 1].position = position;
          position = position == "left" ? "right" : "left";
          feedlist[z].yaxis = yaxes.length;
          break;
        }
      }
    }
  }
}

function graph() {
  build_yaxes();

  var rows = response.data || [];
  var times = rows.map(function(r) { return r[0]; });
  var plotted = feedlist.filter(function(f) { return f.dataindex != undefined; });

  // Each feed's values (scaled, accrued), then the stacked sums, bottom (last feed) first.

  var raw = plotted.map(function(f) {
    var scale = f.scale, accrual = 0;
    return rows.map(function(r) {
      var v = r[f.dataindex];
      var out = v == null ? null : v * scale + accrual;
      if (f.accrue && v != null) accrual += v * scale;
      return out;
    });
  });
  var drawn = raw.map(function(values) { return values.slice(); });
  var below = plotted.map(function() { return -1; });          // the stacked feed each one sits on
  var stackTop = {};                                            // per axis: the last stacked feed
  for (var i = plotted.length - 1; i >= 0; i--) {
    if (!plotted[i].stack) continue;
    var axis = plotted[i].yaxis;
    if (stackTop[axis] !== undefined) {
      var base = drawn[stackTop[axis]];
      below[i] = stackTop[axis];
      drawn[i] = raw[i].map(function(v, k) { return v == null ? null : v + (base[k] == null ? 0 : base[k]); });
    }
    stackTop[axis] = i;
  }

  // Data range per axis, for the y axes table.

  yaxes.forEach(function(y) { y.datamin = null; y.datamax = null; });
  plotted.forEach(function(f, n) {
    var y = yaxes[f.yaxis - 1];
    drawn[n].forEach(function(v) {
      if (v == null) return;
      if (y.datamin == null || v < y.datamin) y.datamin = v;
      if (y.datamax == null || v > y.datamax) y.datamax = v;
    });
  });

  var text = themeColor("--bs-secondary-color") || "#666";
  var grid = themeColor("--bs-border-color") || "#ddd";

  var scales = {
    x: {
      time: true,
      range: function() {
        return [beginDate.viewDate().d.getTime() / 1000, endDate.viewDate().d.getTime() / 1000];
      },
    },
  };
  var axes = [{ stroke: text, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid, width: 1 } }];
  yaxes.forEach(function(y, n) {
    var min = y.min, max = y.max;
    scales[y.unit] = {
      range: function(u, dmin, dmax) {
        var r = dmin == null ? [0, 1] : uPlot.rangeNum(dmin, dmax, 0.1, true);
        if (min !== "" && min != null) r[0] = Number(min);
        if (max !== "" && max != null) r[1] = Number(max);
        return r;
      },
    };
    axes.push({
      scale: y.unit,
      side: y.position == "left" ? 3 : 1,
      stroke: text,
      size: 70,
      grid: { show: n == 0, stroke: grid, width: 1 },
      ticks: { stroke: grid, width: 1 },
      values: function(u, vals) { return vals.map(function(v) { return unitFormat(v, y.unit); }); },
    });
  });

  var uSeries = [{ value: function(u, t) { return t == null ? "" : tooltipTime(new Date(t * 1000)); } }];
  var bands = [];
  plotted.forEach(function(f, n) {
    var bar = f.plottype == "Bar";
    var s = {
      label: (showUnit ? f.unit + ": " : "") + f.name,
      scale: f.unit,
      stroke: f.color,
      width: bar ? 1 : 2,
      spanGaps: !bar,
      points: { show: false },
      value: function(u, v, si, idx) {
        if (idx == null || raw[n][idx] == null) return "–";
        var shown = unitFormat(raw[n][idx], f.unit);
        return below[n] >= 0 ? shown + " (stack " + unitFormat(v, f.unit) + ")" : shown;
      },
    };
    if (bar) {
      s.paths = uPlot.paths.bars({ size: [0.75, Infinity], align: 1 });
      if (f.fill) s.fill = f.color;
      if (below[n] >= 0) bands.push({ series: [n + 1, below[n] + 1], fill: function() { return null; } });  // from the stack below
    } else if (f.fill && below[n] < 0) {
      s.fill = hexAlpha(f.color, 0.4);                         // down to zero
    }
    uSeries.push(s);
  });

  var opts = Object.assign(chartSize(), {
    scales: scales,
    axes: axes,
    series: uSeries,
    bands: bands,
    legend: { show: showLegend, live: true },
    cursor: {
      drag: { x: !embed, y: false, setScale: false },
      points: { show: false },
    },
    hooks: {
      // A stacked, filled line: fill down to the stack below it (bands can't use bars as their lower edge).
      drawSeries: [function(u, si) {
        var n = si - 1;
        var f = plotted[n];
        if (!f || f.plottype == "Bar" || !f.fill || below[n] < 0 || !u.series[si].show) return;
        var top = drawn[n], base = drawn[below[n]], pts = [];
        for (var k = 0; k < times.length; k++) {
          if (top[k] == null) continue;
          pts.push([u.valToPos(times[k], "x", true), u.valToPos(top[k], f.unit, true),
                    u.valToPos(base[k] == null ? 0 : base[k], f.unit, true)]);
        }
        if (pts.length < 2) return;
        var ctx = u.ctx;
        ctx.save();
        ctx.beginPath();
        ctx.rect(u.bbox.left, u.bbox.top, u.bbox.width, u.bbox.height);
        ctx.clip();
        ctx.beginPath();
        pts.forEach(function(p, k) { if (k == 0) ctx.moveTo(p[0], p[1]); else ctx.lineTo(p[0], p[1]); });
        for (var k = pts.length - 1; k >= 0; k--) ctx.lineTo(pts[k][0], pts[k][2]);
        ctx.closePath();
        ctx.fillStyle = hexAlpha(f.color, 0.4);
        ctx.fill();
        ctx.restore();
      }],
      // The plotted range of each axis, for the y axes table (known once the chart has its scales).
      ready: [function(u) {
        yaxes.forEach(function(y) {
          var sc = u.scales[y.unit];
          y.tickmin = sc && sc.min != null ? sc.min : 0;
          y.tickmax = sc && sc.max != null ? sc.max : 0;
        });
        build_yaxes_table();
      }],
      setSelect: [function(u) {
        if (embed || u.select.width < 3) return;
        var from = u.posToVal(u.select.left, "x");
        var to = u.posToVal(u.select.left + u.select.width, "x");
        u.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
        rangeSelected(from, to);
      }],
    },
  });

  var data = [times].concat(drawn);
  if (chart) chart.destroy();
  byId("placeholder").innerHTML = "";
  chart = new uPlot(opts, data, byId("placeholder"));

  // Rebuild the tables to update context

  setShown(".data-tables", false);
  if (feedlist.length != 0) {
    build_options_table();
    build_stats_table();
    build_yaxes_table();
    build_CSV();
    setShown(".data-tables", true);
  }
  build_source_list();
}

// Time in the legend, e.g. "5:17 PM Wed, Nov 25", in the browser's language
function tooltipTime(d) {
  return d.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) + " " +
    d.toLocaleDateString([], { weekday: "short", month: "short", day: "numeric" });
}

//********************************************************************************************
//        Create the tables for options, statistics, yaxes and CSV
//********************************************************************************************

function moveCells(z) {
  var cell = "<td>";
  if (z > 0) {
    cell += "<a class='move-feed' title='Move up' feedid=" + z + " moveby=-1 ><i class='ico ico-arrow-up'></i></a>";
  }
  if (z < feedlist.length - 1) {
    cell += "<a class='move-feed' title='Move down' feedid=" + z + " moveby=1 ><i class='ico ico-arrow-down'></i></a>";
  }
  cell += "</td>";
  cell += "<td><span class='feed-delete' feedindex=" + z + "><i class='ico ico-trash3'></i></span></td>";
  cell += "<td style='text-align:left'>" + esc(feedlist[z].unit + ":" + feedlist[z].name) + "</td>";
  return cell;
}

function build_options_table() {
  var html = "";
  for (var z in feedlist) {
    if (feedlist[z].dataindex == undefined) continue;
    var f = feedlist[z];
    var line = "<tr>" + moveCells(Number(z));
    line += "<td><input class='table-input linecolor' feedindex=" + z + " style='width:50px;' type='color' value='" +
      esc(f.color) + "'></td>";
    line += "<td><button type='button' class='table-input line-bar' feedindex=" + z + ">" + esc(f.plottype) + "</button></td>";
    line += "<td style='text-align:center'><input class='fill' type='checkbox' feedindex=" + z + (f.fill ? " checked" : "") + " /></td>";
    line += "<td style='text-align:center'><input class='stack' type='checkbox' feedindex=" + z + (f.stack ? " checked" : "") + "></td>";
    if (f.unit == "Wh" || f.unit == "VARh") {
      line += "<td style='text-align:center'><input class='accrue' type='checkbox' feedindex=" + z + (f.accrue ? " checked" : "") + "></td>";
    } else {
      line += "<td></td>";
    }
    line += "<td><input class='table-input decimalpoints' feedindex=" + z +
      " type='number' min='0' max='3' step='1' value='" + esc(f.dp) + "' style='width:50px;' /></td>";
    line += "<td><input class='table-input scale' feedindex=" + z + " type='text' style='width:50px;' value='" +
      esc(f.scale) + "' /></td>";
    line += "</tr>";
    html += line;
  }
  byId("sourceOptionsBody").innerHTML = html;
}

function build_stats_table() {
  var html = "";
  for (var z in feedlist) {
    if (feedlist[z].dataindex == undefined) continue;
    var dp = feedlist[z].dp;
    var stats = compute_stats(feedlist[z].dataindex, feedlist[z].scale);
    var line = "<tr>" + moveCells(Number(z));
    var quality = Math.round(100 * (1 - stats.npointsnull / stats.npoints));
    line += "<td>" + quality + "% (" + (stats.npoints - stats.npointsnull) + "/" + stats.npoints + ")</td>";
    line += "<td>" + stats.minval.toFixed(dp) + "</td>";
    line += "<td>" + stats.maxval.toFixed(dp) + "</td>";
    line += "<td>" + stats.diff.toFixed(dp) + "</td>";
    line += "<td>" + unitFormat(stats.mean.toFixed(dp), feedlist[z].unit) + "</td>";

    var unit = feedlist[z].unit;
    line += "<td>";
    for (var u in units) {
      if (units[u].unit == unit) {
        if (units[u].stat == "S") {
          line += unitFormat(stats.sum, units[u].label);
        } else if (units[u].stat == "I") {
          line += unitFormat(((1 - stats.npointsnull / stats.npoints) * stats.mean * periodDuration) / 3600, units[u].unit) + "h";
        }
        break;
      }
    }
    line += "</td></tr>";
    html += line;
  }
  byId("sourceStatsBody").innerHTML = html;
}

function compute_stats(dataindex, scale) {
  var stats = { minval: 0, maxval: 0, sum: 0, npointsnull: 0, npoints: 0 };
  var i = 0;
  var val = null;
  for (var z in response.data) {
    if (response.data[z][dataindex] != null) {
      val = response.data[z][dataindex] * scale;
      if (i == 0) {
        stats.maxval = val;
        stats.minval = val;
      }
      if (val > stats.maxval) stats.maxval = val;
      if (val < stats.minval) stats.minval = val;
      stats.sum += val;
      i++;
    } else stats.npointsnull++;
    stats.npoints++;
  }
  stats.mean = stats.sum / i;
  stats.diff = stats.maxval - stats.minval;
  return stats;
}

function build_yaxes_table() {
  var html = "";
  for (var y in yaxes) {
    var yaxis = yaxes[y];
    var unit = yaxis.unit;
    var ui = unitindex(unit);
    var min = units[ui].min == undefined ? "" : units[ui].min;
    var max = units[ui].max == undefined ? "" : units[ui].max;
    var dp = units[ui].dp;
    var line = "<tr>";
    line += "<td style='text-align:left'>" + yaxis.unit + "</td>";
    line += "<td>" + unitFormat(yaxis.tickmin, unit) + " to " + unitFormat(yaxis.tickmax, unit) + "</td>";
    line += "<td>" + Number(yaxis.datamin).toFixed(dp) + " to " + Number(yaxis.datamax).toFixed(dp) + "</td>";
    line += "<td><input class='table-input ymin text-center' unitindex=" + ui +
      " style='width:50px' placeholder='auto' value='" + esc(min) + "'></td>";
    line += "<td></td>";
    line += "<td><input class='table-input ymax text-center' unitindex=" + ui +
      " style='width:50px' placeholder='auto' value='" + esc(max) + "'></td>";
    line += "</tr>";
    html += line;
  }
  byId("yaxesBody").innerHTML = html;
}

function build_CSV() {
  var out = [];
  var rows = response.data || [];
  if (rows.length == 0) { byId("csv").value = ""; return; }
  var timeformat = byId("csvtimeformat").value;
  var nullvalues = byId("csvnullvalues").value;
  var start_time = rows[0][0];
  var accrual = feedlist.map(function() { return 0; });
  for (var i = 0; i < rows.length; i++) {
    var line = "";
    if (timeformat == "unix") {
      line += rows[i][0];
    } else if (timeformat == "seconds") {
      line += rows[i][0] - start_time;
    } else if (timeformat == "datestr") {
      line += formatDate(new Date(rows[i][0] * 1000), "YYYY-MM-DD HH:mm:ss");
    }

    for (var f in feedlist) {
      var value = rows[i][feedlist[f].dataindex];
      if (value == null) {
        if (nullvalues == "show") {
          line += ", null";
        } else if (nullvalues == "remove") {
          line = "";
          break;
        } else {
          line += ",";
        }
      } else {
        value = value * feedlist[f].scale + accrual[f];
        line += ", " + Number(value).toFixed(feedlist[f].dp);
        if (feedlist[f].accrue) {
          accrual[f] = value;
        }
      }
    }
    if (line.length > 0) out.push(line + "\n");
  }
  byId("csv").value = out.join("");
}

//********************************************************************************************
//            GRAPH SAVE
//********************************************************************************************

byId("graph-select").addEventListener("change", function() {
  loading = true;
  var name = this.value;

  byId("graph-name").value = name;
  setShown(byId("graph-save"), true);
  setShown(byId("graph-delete"), true);
  var index;
  for (index = 0; index < savedgraphs.length; index++) {
    if (name == savedgraphs[index].name) break;
  }
  if (index >= savedgraphs.length) {
    return;
  }

  if (savedgraphs[index].legacy) {
    byId("graph-name").value = savedgraphs[index].legacy.name;
    applyContext(legacyContext(savedgraphs[index].legacy));
    showError("Converted from the original Graph page. Save keeps it as a Graph+ graph; " +
              "Delete removes the original.");
    return;
  }

  requestJSON("GET", path + "/graphs/" + savedgraphs[index].id, null, function(result) { applyContext(result); });
});

function applyContext(context) {
  period = context.period;
  build_period_selector();

  group = context.group;
  build_group_options();

  unit = context.unit;
  build_units_selector();

  userDateChange = false;
  beginDate.date(new Date(context.beginDate));
  endDate.date(new Date(context.endDate));
  userDateChange = true;

  feedlist = context.feedlist;
  refresh = false;
  if (context.refresh !== undefined) {
    refresh = context.refresh;
  }

  for (var y in context.yaxes) {
    var u = unitindex(context.yaxes[y].unit);
    units[u].min = context.yaxes[y].min;
    units[u].max = context.yaxes[y].max;
  }
  loading = false;
  query();
}

byId("graph-save").addEventListener("click", function() {
  var name = byId("graph-name").value;

  if (name == undefined || name == "") {
    alert("Please enter a name for the graph.");
    return false;
  }

  var context = {
    name: name,
    period: custom_dates ? "custom dates" : period,
    group: group,
    unit: unit,
    endDate: endDate.viewDate(),
    beginDate: beginDate.viewDate(),
    feedlist: feedlist,
    yaxes: [],
  };
  if (refresh) {
    context.refresh = true;
  }

  for (var z in context.feedlist) {
    delete context.feedlist[z].stats;
  }
  for (var u in units) {
    if (units[u].min != "" || units[u].max != "") {
      context.yaxes.push({ unit: units[u].unit, min: units[u].min, max: units[u].max });
    }
  }
  // The body as before: "data=" and the JSON, not URL-encoded (what the device has always received).
  requestJSON("POST", path + "/graph/create", "data=" + JSON.stringify(context), function(result) {
    if (!result.success) alert("ERROR: " + result.message);
    setShown(byId("graph-delete"), true);
  });
  graph_load_savedgraphs();
  byId("graph-select").value = name;
});

byId("graph-name").addEventListener("keyup", function() {
  var name = this.value;
  setShown(byId("graph-delete"), false);
  if (name == "") {
    setShown(byId("graph-save"), false);
    return;
  }
  for (var z in savedgraphs) {
    if (savedgraphs[z].name == name) {
      setShown(byId("graph-delete"), true);
      break;
    }
  }
  setShown(byId("graph-save"), true);
});

byId("graph-delete").addEventListener("click", function() {
  var name = byId("graph-name").value;
  for (var z in savedgraphs) {
    if (savedgraphs[z].name == name || (savedgraphs[z].legacy && savedgraphs[z].legacy.name == name)) {
      graph_delete(savedgraphs[z].id);
      byId("graph-name").value = "";
      setShown(byId("graph-delete"), false);
    }
  }
});

function graph_load_savedgraphs(callback) {
  requestJSON("GET", path + "/graph/getallplus", null, function(result) {
    savedgraphs = result;
    // Graphs saved with the original Graph page (retired): listed too, converted when opened (B83).
    requestJSON("GET", path + "/graph/getall", null,
      function(old) {
        for (var i in old) {
          if (old[i] && old[i].name && old[i].id !== undefined) {
            savedgraphs.push({ name: old[i].name + LEGACY_SUFFIX, id: old[i].id, legacy: old[i] });
          }
        }
      },
      null,
      function() {
        var select = byId("graph-select");
        select.innerHTML = "";
        var first = document.createElement("option");
        first.textContent = "Select graph:";
        first.selected = true;
        select.appendChild(first);
        for (var z in savedgraphs) {
          var opt = document.createElement("option");
          opt.textContent = savedgraphs[z].name;
          select.appendChild(opt);
        }
        if (callback) callback();
      });
  });
}

var LEGACY_SUFFIX = " (original Graph page)";

// A graph saved by the original Graph page as a Graph+ context: its feeds (the second letter of a feed
// id is the kind: V volts, P power, E energy), its dates (a "floating" graph keeps its length, ending
// now), automatic grouping.
function legacyContext(g) {
  var unitOf = { V: "Volts", P: "Watts", E: "Wh" };
  var list = [];
  for (var i in g.feedlist || []) {
    var f = g.feedlist[i];
    var u = unitOf[String(f.id || "").charAt(1)];
    if (!u || !f.name) continue;
    list.push({ name: f.name, unit: u, color: f.color || assign_color(), fill: !!f.fill, stack: false,
                scale: f.scale || 1, accrue: false, dp: units[unitindex(u)].dp, plottype: "Line" });
  }
  var end = g.floatingtime ? Date.now() : g.end;
  var begin = end - (g.end - g.start);
  return { period: "custom dates", group: "auto", unit: list.length ? list[0].unit : "Watts",
           beginDate: new Date(begin).toJSON(), endDate: new Date(end).toJSON(), feedlist: list, yaxes: [] };
}

function graph_delete(id) {
  requestJSON("POST", path + "/graph/delete", "id=" + id, function(result) {
    if (!result.success) alert("ERROR: " + result.message);
  });
  graph_load_savedgraphs();
}

//********************************************************************************************
//            INITIALIZE - Onetime startup sequence
//********************************************************************************************

function initialize() {
  build_units_selector();
  query_series_list();
  if (queryParameters.has("embed")) {
    embed = true;
    setShown(".embed", false);
    if (!queryParameters.has("graph")) {
      alert("embed specified with no graph parameter.");
    }
  }
  byId("graph-reset").click();
  if (queryParameters.has("graph")) {
    graph_load_savedgraphs(function() {
      var graph = queryParameters.get("graph");
      var validGraph = false;
      for (var z in savedgraphs) {
        if (graph == savedgraphs[z].name) {
          byId("graph-select").value = graph;
          fire(byId("graph-select"), "change");
          validGraph = true;
          break;
        }
      }
      if (!validGraph) {
        alert("Graph " + queryParameters.get("graph") + " not found or invalid.");
      }
    });
  } else {
    graph_load_savedgraphs();
  }
  graph_resize();
}

function query_series_list() {
  requestJSON("GET", path + "/query?show=series", null, function(data_in) {
    series = data_in.series;
    build_source_list();
  });
}
