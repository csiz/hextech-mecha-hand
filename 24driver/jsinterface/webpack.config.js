const path = require("path");

module.exports = [
  "source-map"
].map(devtool => ({
  mode: "development",
  entry: "./index.js",
  output: {
    path: path.resolve(__dirname, "dist"),
    filename: "24driver.js",
    library: "24driver"
  },
  devtool,
  optimization: {
    runtimeChunk: true
  },
  externals: {
    lodash: {
      commonjs: 'lodash',
      commonjs2: 'lodash',
      amd: 'lodash',
      root: '_',
    },
  },
}));