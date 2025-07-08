callDBus("com.github.CluelessCatBurger.WlShimeji.KWinSupport", "/", "com.github.CluelessCatBurger.WlShimeji.KWinSupport.iface", "new_ie_position", (id, x, y) => {
  for (const win of workspace.stackingOrder) {
    if (x == y && x == -2147483648) continue;
    if (win.internalId.toString() === id) {
      win.frameGeometry = {
        x: x,
        y: y,
        width: win.frameGeometry.width,
        height: win.frameGeometry.height
      };
      if (x >= workspace.workspaceWidth || y >= workspace.workspaceHeight || x + win.frameGeometry.width <= 0 || y + win.frameGeometry.height <= 0) {

        var window = null;
        var is_active = false;
        var order = -1

        for (const _win of workspace.stackingOrder) {
          if (_win.internalId.toString() === id) continue;
          if (_win.layer != 2 || _win.minimized || _win.deleted || (_win.desktops.length != 0 && _win.desktops.indexOf(workspace.currentDesktop) == -1)) continue;
          if (_win.frameGeometry.x >= workspace.workspaceWidth || _win.frameGeometry.y >= workspace.workspaceHeight || _win.frameGeometry.x + _win.frameGeometry.width <= 0 || _win.frameGeometry.y + _win.frameGeometry.height <= 0) continue;
          is_active = !_win.fullScreen;

          if (_win.stackingOrder > order) {
            order = _win.stackingOrder;
            window = _win;
          }
        }
        if (window != null && is_active) {
          workspace.activeWindow = window;
        }
      }
    }
  }
});
