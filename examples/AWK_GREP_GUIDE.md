# AWK/Grep Integration Guide

Export logs from BlobStorage and pipe to standard Unix tools.

## Build

```bash
g++ -O2 -std=c++17 -Iinclude \
  build/blob_storage.o \
  build/blob_storage_io.o \
  build/blob_indexer.o \
  examples/log_storage.cpp \
  examples/logexport.cpp \
  -o bin/logexport \
  -pthread
```

## Output Format

Tab-separated values (TSV):
```
timestamp   level   service   hostname   thread_id   message
```

## Usage Examples

### 1. Grep for Patterns
```bash
# Find all ERROR logs
logexport /var/log/app cat web-server 20260108 | grep ERROR

# Case-insensitive search
logexport /var/log/app cat web-server 20260108 | grep -i timeout

# Exclude patterns
logexport /var/log/app cat web-server 20260108 | grep -v DEBUG

# Multiple patterns
logexport /var/log/app cat api 20260108 | grep -E "ERROR|FATAL"

# Context lines (before/after)
logexport /var/log/app cat api 20260108 | grep -A 3 -B 3 "connection failed"
```

### 2. AWK Field Processing
```bash
# Count logs by level
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '{print $2}' | sort | uniq -c

# Show only ERROR messages
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '$2 == "ERROR" {print $6}'

# Filter by timestamp range
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '$1 >= 1704672000 && $1 <= 1704675600'

# Count by service and level
logexport /var/log/app query web-server 20260101 20261231 | awk -F'\t' '{key=$3":"$2; count[key]++} END {for (k in count) print k, count[k]}'

# Calculate avg response time (if in message)
logexport /var/log/app cat api 20260108 | awk -F'\t' '$6 ~ /response_time=[0-9]+/ {match($6, /response_time=([0-9]+)/, arr); sum+=arr[1]; count++} END {print sum/count}'

# Print formatted with timestamps
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '{print strftime("%Y-%m-%d %H:%M:%S", $1), $2, $6}'
```

### 3. AWK Complex Filters
```bash
# Errors containing "timeout"
logexport /var/log/app cat api 20260108 | awk -F'\t' '$2 == "ERROR" && $6 ~ /timeout/'

# Multiple conditions
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '($2 == "ERROR" || $2 == "FATAL") && $6 ~ /(timeout|connection)/'

# Time range + pattern
logexport /var/log/app cat api 20260108 | awk -F'\t' '$1 >= 1704672000 && $1 <= 1704675600 && $6 ~ /database/'

# Exclude DEBUG/INFO
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '$2 != "DEBUG" && $2 != "INFO"'

# Custom formatting
logexport /var/log/app cat api 20260108 | awk -F'\t' '{printf "[%s] %s: %s\n", $2, $3, $6}'
```

### 4. Sed Transformations
```bash
# Replace patterns
logexport /var/log/app cat web-server 20260108 | sed 's/ERROR/[!!!ERROR!!!]/g'

# Extract specific fields and reformat
logexport /var/log/app cat api 20260108 | sed -E 's/^([0-9]+)\t([A-Z]+)\t.*\t(.*)$/\1 [\2] \3/'

# Remove thread IDs
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '{print $1"\t"$2"\t"$3"\t"$4"\t"$6}'
```

### 5. Combined Pipelines
```bash
# Top 10 error messages
logexport /var/log/app cat web-server 20260108 | \
  grep ERROR | \
  awk -F'\t' '{print $6}' | \
  sort | uniq -c | \
  sort -rn | \
  head -10

# Errors per hour
logexport /var/log/app cat web-server 20260108 | \
  grep ERROR | \
  awk -F'\t' '{hour=strftime("%Y-%m-%d %H:00", $1); count[hour]++} END {for (h in count) print h, count[h]}' | \
  sort

# Find slowest requests
logexport /var/log/app cat api 20260108 | \
  awk -F'\t' '$6 ~ /duration=[0-9]+/ {match($6, /duration=([0-9]+)/, arr); print arr[1]"\t"$0}' | \
  sort -rn | \
  head -20

# Count unique hosts with errors
logexport /var/log/app cat web-server 20260108 | \
  grep ERROR | \
  awk -F'\t' '{print $4}' | \
  sort -u | \
  wc -l

# Alert on threshold (exit 1 if > 100 errors)
ERROR_COUNT=$(logexport /var/log/app cat api 20260108 | grep ERROR | wc -l)
if [ $ERROR_COUNT -gt 100 ]; then
  echo "ALERT: $ERROR_COUNT errors detected!"
  exit 1
fi
```

### 6. Statistics with AWK
```bash
# Summary stats by level
logexport /var/log/app cat web-server 20260108 | \
  awk -F'\t' '
  {
    count[$2]++
    total++
  }
  END {
    for (level in count) {
      printf "%s: %d (%.2f%%)\n", level, count[level], (count[level]/total)*100
    }
  }'

# Logs per minute
logexport /var/log/app cat api 20260108 | \
  awk -F'\t' '
  {
    minute = int($1 / 60)
    count[minute]++
  }
  END {
    for (m in count) {
      print strftime("%Y-%m-%d %H:%M", m*60), count[m]
    }
  }' | sort

# Distribution by thread
logexport /var/log/app cat web-server 20260108 | \
  awk -F'\t' '{threads[$5]++} END {for (t in threads) print t, threads[t]}' | \
  sort -k2 -rn
```

### 7. Monitoring Scripts
```bash
#!/bin/bash
# monitor-errors.sh - Alert on error rate spike

ROOT="/var/log/app"
SERVICE="web-server"
DATE=$(date +%Y%m%d)
THRESHOLD=50

while true; do
  ERROR_COUNT=$(logexport $ROOT tail $SERVICE 1000 | grep ERROR | wc -l)
  
  if [ $ERROR_COUNT -gt $THRESHOLD ]; then
    echo "$(date): ALERT! $ERROR_COUNT errors in last 1000 logs"
    # Send alert (email, slack, etc.)
  fi
  
  sleep 60
done
```

### 8. Log Analysis Report
```bash
#!/bin/bash
# log-report.sh - Generate daily report

DATE=$1
ROOT="/var/log/app"

echo "=== Log Report for $DATE ==="
echo ""

echo "Services:"
logexport $ROOT services
echo ""

for SERVICE in $(logexport $ROOT services | awk '{print $1}'); do
  echo "=== $SERVICE ==="
  
  echo "Total logs:"
  logexport $ROOT cat $SERVICE $DATE | wc -l
  
  echo "By level:"
  logexport $ROOT cat $SERVICE $DATE | awk -F'\t' '{print $2}' | sort | uniq -c
  
  echo "Top 5 errors:"
  logexport $ROOT cat $SERVICE $DATE | grep ERROR | awk -F'\t' '{print $6}' | sort | uniq -c | sort -rn | head -5
  
  echo ""
done
```

### 9. Real-time Monitoring (Tail + Watch)
```bash
# Watch last 20 logs, refresh every 2 seconds
watch -n 2 'logexport /var/log/app tail web-server 20 | tail -20'

# Stream errors in real-time (with timestamps)
logexport /var/log/app tail web-server 1000 | grep ERROR | awk -F'\t' '{print strftime("%H:%M:%S", $1), $6}'
```

### 10. Export to CSV for Analysis
```bash
# Convert to CSV with headers
echo "timestamp,level,service,hostname,thread_id,message" > logs.csv
logexport /var/log/app cat web-server 20260108 | tr '\t' ',' >> logs.csv

# Import to SQLite
sqlite3 logs.db <<EOF
CREATE TABLE logs(timestamp INTEGER, level TEXT, service TEXT, hostname TEXT, thread_id TEXT, message TEXT);
.mode tabs
.import "|logexport /var/log/app cat web-server 20260108" logs
SELECT level, COUNT(*) FROM logs GROUP BY level;
EOF
```

## Performance Tips

1. **Use date filters** to limit data:
```bash
# Good: Query specific date
logexport /var/log/app cat web-server 20260108 | grep ERROR

# Bad: Query all dates (slow)
logexport /var/log/app query web-server 20260101 20261231 | grep ERROR
```

2. **Filter early** in the pipeline:
```bash
# Good: Filter at source
logexport /var/log/app cat web-server 20260108 | awk -F'\t' '$2=="ERROR"' | grep timeout

# Better: Use query with level filter (if supported)
logexport /var/log/app cat web-server 20260108 | grep ERROR | grep timeout
```

3. **Use parallel for multiple services**:
```bash
for service in web api worker; do
  logexport /var/log/app cat $service 20260108 | grep ERROR &
done
wait
```

## Integration with Existing Tools

### Logrotate Config
```
/var/log/app/*.log {
    daily
    rotate 30
    compress
    postrotate
        # Export and archive
        logexport /var/log/app cat $SERVICE $(date +%Y%m%d -d yesterday) | gzip > /archive/$SERVICE-$(date +%Y%m%d).gz
    endscript
}
```

### Cron Jobs
```cron
# Daily error summary
0 1 * * * logexport /var/log/app cat web-server $(date +%Y%m%d -d yesterday) | grep ERROR | mail -s "Daily Errors" admin@example.com

# Hourly stats
0 * * * * logexport /var/log/app tail web-server 1000 | awk -F'\t' '{print $2}' | sort | uniq -c > /var/stats/hourly.txt
```

### Prometheus Exporter
```bash
#!/bin/bash
# Export metrics for Prometheus

while true; do
  logexport /var/log/app tail web-server 10000 | \
    awk -F'\t' '{count[$2]++} END {
      for (level in count) {
        print "log_entries_total{level=\""level"\"} "count[level]
      }
    }' > /var/metrics/logs.prom
  
  sleep 60
done
```

## Quick Reference

```bash
# Show all logs
logexport <root> cat <service> <date>

# Count by level
... | awk -F'\t' '{print $2}' | sort | uniq -c

# Filter by level
... | awk -F'\t' '$2 == "ERROR"'

# Filter by time
... | awk -F'\t' '$1 >= START && $1 <= END'

# Extract message only
... | awk -F'\t' '{print $6}'

# Format timestamp
... | awk -F'\t' '{print strftime("%Y-%m-%d %H:%M:%S", $1), $6}'
```
