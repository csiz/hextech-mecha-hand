const path = require('path');
const copy_plugin = require('copy-webpack-plugin');

module.exports = {
  mode: "development",
  entry: './index.js',
  output: {
    filename: 'main.js',
    path: path.resolve(__dirname, 'dist'),
  },
  plugins: [
    new copy_plugin({
      patterns: [
        { from: './index.html', to: 'index.html' }
      ],
    }),
  ],
  devtool: "inline-source-map",
  devServer: {
    static: {
      directory: "./dist"
    }
  },
  resolve: {
    alias: {
      "hexhand": path.resolve(__dirname, "../24driver/jsinterface")
    }
  }
};