# ---------- Build stage ----------
FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy everything (includes submodules already present in repo)
COPY . .

# Configure + build (Release)
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target api_server -j

# ---------- Runtime stage ----------
FROM debian:bookworm-slim AS runtime

# Create non-root user
RUN useradd -r -u 10001 appuser

WORKDIR /app

# Copy binary
COPY --from=build /app/build/api_server /app/api_server

# Copy runtime data (stopwords)
COPY --from=build /app/data/stopwords_de.txt /app/data/stopwords_de.txt

ENV PORT=8080
ENV STOPWORDS_FILE=/app/data/stopwords_de.txt

EXPOSE 8080

USER appuser

CMD ["/app/api_server"]
