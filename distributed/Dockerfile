FROM python:3.11-slim
RUN apt-get update && apt-get install -y --no-install-recommends default-libmysqlclient-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY distributed /app/distributed
COPY database/migrations /app/database/migrations
