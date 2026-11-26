# Bootstrap for the web UI

The web UI pages (`SD/index.htm`, `graph2.htm`, `edit.htm`) share one look, built on
[Bootstrap](https://getbootstrap.com) 5.3. Only the parts the pages use are included, with the IoTaWatt theme, so
the two files cost about 29 KB of flash (gzipped) in the embedded web UI:

| File | What | Size (gzip) |
|---|---|---|
| `SD/bootstrap.min.css` | reboot, grid, forms, buttons, tables, navbar, dropdown, offcanvas, cards, alerts, list group, a subset of utilities, light and dark themes, the icons the pages use (from [Bootstrap Icons](https://icons.getbootstrap.com)) | about 20 KB |
| `SD/bootstrap.min.js` | Collapse, Dropdown, Offcanvas (no Popper: every dropdown is in the menu bar) | about 9 KB |

Both are built files, committed so that the firmware build needs no Node.js. Change them only through this
directory:

```sh
cd Firmware/scripts/webui-bootstrap
npm ci            # the exact versions in package-lock.json
npm run build     # writes ../../../SD/bootstrap.min.css and .js
```

- **Theme, included components, utilities**: `src/iotawatt-bootstrap.scss`.
- **JavaScript components**: `src/iotawatt-bootstrap.js`. A dropdown outside the menu bar needs real Popper:
  remove the `--alias` in `build.sh`.
- **Icons**: the list in `icons.js`; use them as `<i class="ico ico-NAME"></i>`.

Page-specific styles are plain CSS in `SD/cnfstyle.css`; the shared menu bar is `SD/iotanav.js`.

Licences: Bootstrap and Bootstrap Icons are MIT; their notices are kept in the built files.
