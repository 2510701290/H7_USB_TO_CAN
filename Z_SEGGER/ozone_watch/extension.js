const vscode = require('vscode');
const net = require('net');

const HOST = '127.0.0.1';
const PORT = 19200;
const WINDOW = 'Data Sampling';   // 目标窗口：数据采样窗口（原 Data Graph）

function send(commands) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host: HOST, port: PORT });
    const fail = (err) => { sock.destroy(); reject(err); };
    sock.setTimeout(3000, () => fail(new Error(`连接 Ozone (${HOST}:${PORT}) 超时，Ozone 在运行吗？`)));
    sock.once('error', fail);
    sock.once('connect', () => {
      sock.setTimeout(0);
      sock.write(commands);
      setTimeout(() => { sock.end(); resolve(); }, 200);
    });
  });
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand('ozone-data-sampling.addSelection', async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor) return;

      const exprs = editor.selections
        .map(s => editor.document.getText(s).trim())
        .filter(s => s.length > 0)
        .map(s => s.replace(/\\/g, '\\\\').replace(/"/g, '\\"'));   // 转义

      if (exprs.length === 0) {
        vscode.window.showWarningMessage('先选中变量名/表达式再执行。');
        return;
      }

      const cmds = [
        `Window.Show ("${WINDOW}");`,
        ...exprs.map(e => `Window.Add ("${WINDOW}", "${e}");`)
      ].join('\n') + '\n';

      try {
        await send(cmds);
        vscode.window.setStatusBarMessage(`已加入 Ozone Data Sampling: ${exprs.join(', ')}`, 4000);
      } catch (e) {
        vscode.window.showErrorMessage(`发送到 Ozone 失败: ${e.message}`);
      }
    })
  );
}

module.exports = { activate, deactivate() {} };