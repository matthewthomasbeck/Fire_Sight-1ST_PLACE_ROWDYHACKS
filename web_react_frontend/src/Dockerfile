FROM node:22-alpine AS build
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .

# VITE_* values are baked into the bundle at build time.
# ARG alone is not enough: export as ENV so Vite sees them during `vite build`.
ARG VITE_STREAM_URL=/api/v1/streams/stream_1/jpeg
ARG VITE_API_BASE_URL=
ARG VITE_LOGIN_URL=
ARG VITE_DEMO_USER=rowdy
ARG VITE_DEMO_PASSWORD=change-me
ENV VITE_STREAM_URL=$VITE_STREAM_URL \
    VITE_API_BASE_URL=$VITE_API_BASE_URL \
    VITE_LOGIN_URL=$VITE_LOGIN_URL \
    VITE_DEMO_USER=$VITE_DEMO_USER \
    VITE_DEMO_PASSWORD=$VITE_DEMO_PASSWORD

RUN npx vite build --base=/

FROM nginx:alpine
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 80
