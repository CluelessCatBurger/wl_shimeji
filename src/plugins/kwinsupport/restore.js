for (let win of workspace.stackingOrder) {
  if (win.layer != 2) continue;
  const x = win.frameGeometry.x;
  const y = win.frameGeometry.y;
  const width = win.frameGeometry.width;
  const height = win.frameGeometry.height;

  if (x >= workspace.workspaceWidth || y >= workspace.workspaceHeight || x + width <= 0 || y + height <= 0) {
    win.frameGeometry = {
      x: 100,
      y: 100,
      width: width,
      height: height
    }
  }
}
