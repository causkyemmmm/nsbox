FROM python:3.11-alpine

WORKDIR /app
COPY server.py /app/server.py
EXPOSE 8090

CMD ["python3", "/app/server.py", "--config", "/app/sites.json", "--listen", "0.0.0.0", "--port", "8090", "--public-base", "http://192.168.50.161:8090"]
