const path = require('path');
const fs = require('fs');
const rspack = require('@rspack/core');
const HtmlRspackPlugin = require('html-rspack-plugin');
const { minify: minifyHtml } = require('html-minifier-terser');

const buildMode = (process.env.BUILD_MODE || 'web').toLowerCase();
if (buildMode !== 'web' && buildMode !== 'single') {
  console.warn(`[rspack.config] 未知的 BUILD_MODE "${buildMode}"，按 web 处理（可选: web | single）`);
}
const isSingle = buildMode === 'single';
const isDev = process.env.NODE_ENV !== 'production';
const isAnalyze = process.env.ANALYZE === '1';

class InlineSingleFilePlugin {
  apply(compiler) {
    const faviconDataUri =
      'data:image/png;base64,' +
      fs.readFileSync(path.resolve(__dirname, 'public/favicon.png')).toString('base64');

    const readAsset = (compilation, name) => {
      const asset = compilation.getAsset(name);
      const source = asset ? asset.source : compilation.assets[name];
      return source ? String(source.source()) : null;
    };

    compiler.hooks.thisCompilation.tap('InlineSingleFilePlugin', (compilation) => {
      HtmlRspackPlugin.getHooks(compilation).alterAssetTagGroups.tap(
        'InlineSingleFilePlugin',
        (data) => {
          const inlineTag = (tag) => {
            const attrs = tag.attributes || {};
            const src = (attrs.src || attrs.href || '').replace(/^\.\//, '');
            if (tag.tagName === 'script' && src) {
              const code = readAsset(compilation, src);
              return { tagName: 'script', innerHTML: code, closeTag: true, attributes: {}, meta: tag.meta };
            }
            if (tag.tagName === 'link' && /\.css($|\?)/.test(src)) {
              const css = readAsset(compilation, src);
              return { tagName: 'style', innerHTML: css, closeTag: true, attributes: {}, meta: tag.meta };
            }
            if (tag.tagName === 'link' && /favicon\.png$/.test(src)) {
              return {
                tagName: 'link',
                voidTag: true,
                attributes: { rel: attrs.rel, type: attrs.type, href: faviconDataUri },
                meta: tag.meta,
              };
            }
            return tag;
          };
          data.headTags = data.headTags.map(inlineTag);
          data.bodyTags = data.bodyTags.map(inlineTag);
          return data;
        }
      );

      compilation.hooks.processAssets.tap(
        { name: 'InlineSingleFilePlugin', stage: rspack.Compilation.PROCESS_ASSETS_STAGE_REPORT },
        () => {
          const htmlName = Object.keys(compilation.assets).find((n) => n.endsWith('.html'));
          if (htmlName) {
            const html = String(compilation.assets[htmlName].source());
            const updated = html.replace(/href=["']favicon\.png["']/g, `href="${faviconDataUri}"`);
            compilation.updateAsset(htmlName, new rspack.sources.RawSource(updated));
          }
          for (const name of Object.keys(compilation.assets)) {
            if (name.endsWith('.js') || name.endsWith('.css')) {
              compilation.deleteAsset(name);
            }
          }
        }
      );
    });
  }
}

module.exports = {
  context: __dirname,
  entry: {
    main: './src/main.js',
  },
  mode: isDev ? 'development' : 'production',
  devtool: isDev ? 'eval-source-map' : false,
  output: {
    path: path.resolve(__dirname, isSingle ? 'dist' : 'dist/www'),
    clean: !isSingle,
    filename: isSingle ? 'assets/[name].js' : 'js/[name].[contenthash:8].js',
  },
  module: {
    rules: [
      {
        test: /\.css$/,
        use: [rspack.CssExtractRspackPlugin.loader, 'css-loader'],
      },
    ],
  },
  plugins: [
    new HtmlRspackPlugin({
      template: './index.html',
      filename: isSingle ? 'epd.html' : 'index.html',
      minify: (html) => minifyHtml(html, { collapseWhitespace: true, removeComments: true }),
    }),
    new rspack.CssExtractRspackPlugin({
      filename: isSingle ? 'assets/main.css' : 'css/main.[contenthash:8].css',
    }),
    ...(!isSingle
      ? [new rspack.CopyRspackPlugin({ patterns: [{ from: 'public', to: '.' }] })]
      : []),
    ...(isSingle ? [new InlineSingleFilePlugin()] : []),
    ...(isAnalyze
      ? [
          new (require('@rsdoctor/rspack-plugin').RsdoctorRspackPlugin)({
            disableClientServer: false,
          }),
        ]
      : []),
  ],
  ...(isDev || isSingle
    ? {}
    : {
        optimization: {
          runtimeChunk: 'single',
          splitChunks: {
            cacheGroups: {
              dithering: {
                test: /[\\/]src[\\/]dithering\.js$/,
                name: 'dithering',
                chunks: 'all',
                enforce: true,
              },
              rle: {
                test: /[\\/]src[\\/]rle\.js$/,
                name: 'rle',
                chunks: 'all',
                enforce: true,
              },
              dfu: {
                test: /[\\/](dfu\.js$|fflate[\\/]|crc-32[\\/])/,
                name: 'dfu',
                chunks: 'all',
                enforce: true,
              },
            },
          },
        },
      }),
  devServer: {
    port: 8080,
    hot: true,
  },
};
