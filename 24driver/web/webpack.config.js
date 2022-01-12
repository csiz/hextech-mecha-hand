const path = require('path');
const copy_plugin = require('copy-webpack-plugin');

module.exports = {
  mode: 'production',
  entry: './index.js',
  output: {
    path: path.resolve(__dirname, 'dist'),
    filename: 'bundle.js'
  },
  plugins: [
    new copy_plugin({
      patterns: [
        { from: './index.html', to: 'index.html' }
      ],
    }),
  ],
  resolve: {
    alias: {
      "hexhand": path.resolve(__dirname, "../jsinterface")
    }
  }
};