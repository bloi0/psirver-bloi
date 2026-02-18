#!/bin/bash
# Test script for psirver

echo "Testing psirver endpoints..."
echo ""

echo "=== Test 1: GET /health ==="
curl -i http://localhost:8000/health
echo ""

echo "=== Test 2: GET /teapot ==="
curl -i http://localhost:8000/teapot
echo ""

echo "=== Test 3: GET /jobs ==="
curl -i http://localhost:8000/jobs
echo ""

echo "=== Test 4: GET /scripts ==="
curl -i http://localhost:8000/scripts
echo ""

echo "=== Test 5: GET /jobs/123 (should be 404) ==="
curl -i http://localhost:8000/jobs/123
echo ""

echo "=== Test 6: GET /unknown (should be 404) ==="
curl -i http://localhost:8000/unknown
echo ""

echo "=== All tests complete! ==="
