FROM node:18.12.1

MAINTAINER vincebel version: 0.1

WORKDIR /app

COPY package*.json ./

RUN npm install

COPY . .

EXPOSE 8080

CMD ["node", "app.js"]
