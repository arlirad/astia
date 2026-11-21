# Astia frontend

Static project portal for [Astia](https://github.com/arlirad/astia), built with Angular 20 standalone components and SCSS.

The portal presents the project overview, capabilities, near-term roadmap, build instructions, and documentation links. It is a static site: there is no backend or project API.

## Development

```sh
npm install
npm start
```

The dev server defaults to `http://localhost:4200`.

## Checks

```sh
npm run build
npm run test:ci
```

`npm run build` uses the production configuration. `npm run test:ci` runs the Karma/Jasmine suite in headless Chrome.

## Documentation content

The in-app docs browser reads bundled Markdown from the repository `docs/` directory and the project README files. Regenerate the bundle with:

```sh
npm run docs:generate
```

The same generator runs automatically before `npm run build`. Edit the source Markdown in the repository, then rebuild.

## Assets

Brand assets are copied from the repository `img/` directory into `public/img/`. The banner is used in the overview; the 64px logo is used for the header and favicon.
