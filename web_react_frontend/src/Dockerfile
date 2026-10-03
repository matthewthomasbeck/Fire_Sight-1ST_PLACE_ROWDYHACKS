FROM node:22-alpine AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .

# VITE_* values are baked into the bundle at build time.
ARG VITE_STREAM_URL
ARG VITE_LOGIN_URL
ARG VITE_DEMO_USER
ARG VITE_DEMO_PASSWORD
# vite.config.js sets base for GitHub Pages; serve from root here.
RUN npx vite build --base=/

FROM nginx:alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
