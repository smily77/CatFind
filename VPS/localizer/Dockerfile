FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY localizer.py .
# Fallback-Karte ins Image backen (wird nur genutzt, wenn das Repo nicht erreichbar ist)
COPY RasenKarte.csv .

ENV PORT=8080
EXPOSE 8080

CMD ["python", "localizer.py"]
