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
    }
  }
});
