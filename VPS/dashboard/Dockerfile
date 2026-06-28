FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY dashboard.py .
COPY static ./static
# Fallback-Karte (nur falls das Repo nicht erreichbar ist)
COPY RasenKarte.csv .

ENV PORT=80
EXPOSE 80

CMD ["python", "dashboard.py"]
