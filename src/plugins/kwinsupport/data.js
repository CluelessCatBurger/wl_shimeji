function curPosChanged() {
  callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "cursor", workspace.cursorPos.x, workspace.cursorPos.y, () => {});
}

function activeIeChanged() {

  var win = null;
  var order = -1;
  var is_active = false;
  for (const _win of workspace.stackingOrder) {
    if (_win.layer != 2 || _win.minimized || _win.deleted || (_win.desktops.length != 0 && _win.desktops.indexOf(workspace.currentDesktop) == -1)) continue;
    is_active = !_win.fullScreen;

    if (_win.stackingOrder > order && is_active) {
      order = _win.stackingOrder;
      win = _win;
    }
  }

  if (!is_active) {
    callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", false, x, y, width, height, () => {});
    return;
  }

  var x = Math.floor(win.frameGeometry.x);
  var y = Math.floor(win.frameGeometry.y);
  var width = Math.floor(win.frameGeometry.width);
  var height = Math.floor(win.frameGeometry.height);

  callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "active_ie", true, x, y, width, height, () => {});
}

workspace.cursorPosChanged.connect(curPosChanged);

workspace.windowAdded.connect((win) => {
  if (win.layer != 2) return;
  win.visibleGeometryChanged.connect(activeIeChanged);
  win.stackingOrderChanged.connect(activeIeChanged);
  win.minimizedChanged.connect(activeIeChanged);
  win.fullScreenChanged.connect(activeIeChanged);
  win.activeChanged.connect(activeIeChanged);
  win.closed.connect(activeIeChanged);
  activeIeChanged();

});
workspace.windowRemoved.connect((win) => {
  activeIeChanged();
  if (win.layer != 2) return;
  win.visibleGeometryChanged.disconnect(activeIeChanged);
  win.stackingOrderChanged.disconnect(activeIeChanged);
  win.minimizedChanged.disconnect(activeIeChanged);
  win.fullScreenChanged.disconnect(activeIeChanged);
  win.activeChanged.disconnect(activeIeChanged);
  win.closed.disconnect(activeIeChanged);
});

workspace.windowActivated.connect(() => {
  activeIeChanged();
});

workspace.currentDesktopChanged.connect(() => { activeIeChanged() });
workspace.windowActivated.connect(activeIeChanged);

for (const win of workspace.stackingOrder) {
  if (win.layer != 2) continue;
  win.visibleGeometryChanged.connect(activeIeChanged);
  win.stackingOrderChanged.connect(activeIeChanged);
}

activeIeChanged();
