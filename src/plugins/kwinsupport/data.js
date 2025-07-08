function curPosChanged() {
  callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "cursor", workspace.cursorPos.x, workspace.cursorPos.y, () => {});
}

var topmost = null;

function activeIeChanged() {

  var win = null;
  var order = -1;
  var is_active = (topmost != null) ? !topmost.fullScreen : false;
  var was_active = is_active;
  for (const _win of workspace.stackingOrder) {
    if (_win.layer != 2 || _win.minimized || _win.deleted || (_win.desktops.length != 0 && _win.desktops.indexOf(workspace.currentDesktop) == -1)) continue;
    if (_win.frameGeometry.x >= workspace.workspaceWidth || _win.frameGeometry.y >= workspace.workspaceHeight || _win.frameGeometry.x + _win.frameGeometry.width <= 0 || _win.frameGeometry.y + _win.frameGeometry.height <= 0) continue;
    is_active = !_win.fullScreen;

    if (_win.stackingOrder > order) {
      order = _win.stackingOrder;
      win = _win;
    }
  }

  if (topmost != win && topmost != null && was_active) {
    callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
  }
  topmost = win;

  if (!is_active) {
    callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", "", false, 0, 0, 0, 0, () => {});
    return;
  }

  var x = Math.floor(win.frameGeometry.x);
  var y = Math.floor(win.frameGeometry.y);
  var width = Math.floor(win.frameGeometry.width);
  var height = Math.floor(win.frameGeometry.height);

  callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", win.internalId.toString(), true, x, y, width, height, () => {});
}

workspace.cursorPosChanged.connect(curPosChanged);

workspace.windowAdded.connect((win) => {
  if (win.layer != 2) return;
  win.visibleGeometryChanged.connect(activeIeChanged);
  win.stackingOrderChanged.connect(activeIeChanged);
  win.minimizedChanged.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", "", false, 0, 0, 0, 0, () => {});
      activeIeChanged();
    }
  });
  win.fullScreenChanged.connect(activeIeChanged);
  win.activeChanged.connect(activeIeChanged);
  win.interactiveMoveResizeStarted.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
    }
  });
  win.interactiveMoveResizeFinished.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
    }
  });
  win.interactiveMoveResizeStepped.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
    }
  });
  win.closed.connect(activeIeChanged);
  activeIeChanged();

});
workspace.windowRemoved.connect((win) => {
  activeIeChanged();
  if (win.layer != 2) return;
  win.visibleGeometryChanged.disconnect(activeIeChanged);
  win.stackingOrderChanged.disconnect(activeIeChanged);
  win.minimizedChanged.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", "", false, 0, 0, 0, 0, () => {});
      activeIeChanged();
    }
  });
  win.fullScreenChanged.disconnect(activeIeChanged);
  win.activeChanged.disconnect(activeIeChanged);
  win.closed.disconnect(activeIeChanged);
});

workspace.currentDesktopChanged.connect(() => { activeIeChanged() });
workspace.windowActivated.connect(activeIeChanged);

for (const win of workspace.stackingOrder) {
  if (win.layer != 2) continue;
  win.visibleGeometryChanged.connect(activeIeChanged);
  win.stackingOrderChanged.connect(activeIeChanged);
  win.interactiveMoveResizeStarted.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
    }
  });
  win.interactiveMoveResizeFinished.connect(() => {
    if (win == topmost) {
      callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "window_moved_hint", () => {});
    }
  });
}

activeIeChanged();
