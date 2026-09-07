#!/usr/bin/env python3
"""
TriCord Presence Backend API Test Suite
Tests all existing and new Phase C endpoints
"""

import requests
import sys
import hashlib
from typing import Dict, Any

# Base URL from frontend/.env
BASE_URL = "https://deafbfd3-a99b-4179-965c-fe617d9edeab.preview.emergentagent.com"
API_BASE = f"{BASE_URL}/api"

# Test results tracking
test_results = {
    "passed": [],
    "failed": [],
    "errors": []
}

def log_test(name: str, passed: bool, details: str = ""):
    """Log test result"""
    status = "✅ PASS" if passed else "❌ FAIL"
    print(f"{status}: {name}")
    if details:
        print(f"  Details: {details}")
    
    if passed:
        test_results["passed"].append(name)
    else:
        test_results["failed"].append({"name": name, "details": details})

def log_error(name: str, error: str):
    """Log test error"""
    print(f"⚠️  ERROR: {name}")
    print(f"  Error: {error}")
    test_results["errors"].append({"name": name, "error": error})

def generate_icon_data(size: int = 4608) -> bytes:
    """Generate test icon data (gradient pattern)"""
    return bytes(range(256)) * (size // 256) + bytes(range(size % 256))

def test_root_endpoint():
    """Test GET /api/"""
    try:
        response = requests.get(f"{API_BASE}/", timeout=10)
        if response.status_code == 200 and response.json().get("message") == "Hello World":
            log_test("GET /api/ returns Hello World", True)
        else:
            log_test("GET /api/ returns Hello World", False, 
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("GET /api/", str(e))

def test_installer_info():
    """Test GET /api/installer/info"""
    try:
        response = requests.get(f"{API_BASE}/installer/info", timeout=10)
        if response.status_code == 200:
            data = response.json()
            required_fields = ["filename", "title", "version", "size", "sha256", "download_path"]
            missing = [f for f in required_fields if f not in data]
            if missing:
                log_test("GET /api/installer/info returns metadata", False,
                        f"Missing fields: {missing}")
            else:
                log_test("GET /api/installer/info returns metadata", True,
                        f"Size: {data['size']} bytes, SHA256: {data['sha256'][:16]}...")
        else:
            log_test("GET /api/installer/info returns metadata", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("GET /api/installer/info", str(e))

def test_download_cia():
    """Test GET /api/download/tricord-presence-installer.cia"""
    try:
        # Test GET
        response = requests.get(f"{API_BASE}/download/tricord-presence-installer.cia", 
                               timeout=30, stream=True)
        if response.status_code == 200:
            content_type = response.headers.get("Content-Type", "")
            if "octet-stream" in content_type:
                log_test("GET /api/download/tricord-presence-installer.cia serves binary", True,
                        f"Content-Type: {content_type}")
            else:
                log_test("GET /api/download/tricord-presence-installer.cia serves binary", False,
                        f"Wrong Content-Type: {content_type}")
        else:
            log_test("GET /api/download/tricord-presence-installer.cia serves binary", False,
                    f"Status: {response.status_code}")
        
        # Test HEAD
        response = requests.head(f"{API_BASE}/download/tricord-presence-installer.cia", 
                                timeout=10)
        if response.status_code == 200:
            log_test("HEAD /api/download/tricord-presence-installer.cia works", True)
        else:
            log_test("HEAD /api/download/tricord-presence-installer.cia works", False,
                    f"Status: {response.status_code}")
    except Exception as e:
        log_error("GET/HEAD /api/download/tricord-presence-installer.cia", str(e))

def test_status_endpoints():
    """Test POST and GET /api/status"""
    try:
        # Test POST
        test_client = f"test_client_{hashlib.md5(str(id({})).encode()).hexdigest()[:8]}"
        response = requests.post(f"{API_BASE}/status", 
                                json={"client_name": test_client},
                                timeout=10)
        if response.status_code == 200:
            data = response.json()
            if data.get("client_name") == test_client and "id" in data and "timestamp" in data:
                log_test("POST /api/status creates status check", True,
                        f"Created with ID: {data['id']}")
            else:
                log_test("POST /api/status creates status check", False,
                        f"Response missing fields: {data}")
        else:
            log_test("POST /api/status creates status check", False,
                    f"Status: {response.status_code}, Body: {response.text}")
        
        # Test GET
        response = requests.get(f"{API_BASE}/status", timeout=10)
        if response.status_code == 200:
            data = response.json()
            if isinstance(data, list):
                log_test("GET /api/status lists status checks", True,
                        f"Found {len(data)} status checks")
            else:
                log_test("GET /api/status lists status checks", False,
                        f"Expected list, got: {type(data)}")
        else:
            log_test("GET /api/status lists status checks", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("POST/GET /api/status", str(e))

def test_icon_upload_valid():
    """Test POST /api/icons/{tid} with valid 4608 bytes"""
    try:
        tid = "0004000000164800"  # Valid 16-char hex TID
        icon_data = generate_icon_data(4608)
        
        response = requests.post(f"{API_BASE}/icons/{tid}",
                                data=icon_data,
                                headers={"Content-Type": "application/octet-stream"},
                                timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            if (data.get("tid") == tid.upper() and 
                data.get("icon_url") == f"/api/icons/{tid.upper()}.png" and
                "size" in data):
                log_test("POST /api/icons/{tid} with valid 4608 bytes", True,
                        f"TID: {data['tid']}, PNG size: {data['size']} bytes")
                return tid.upper()
            else:
                log_test("POST /api/icons/{tid} with valid 4608 bytes", False,
                        f"Response missing fields or wrong values: {data}")
        else:
            log_test("POST /api/icons/{tid} with valid 4608 bytes", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("POST /api/icons/{tid} with valid 4608 bytes", str(e))
    return None

def test_icon_upload_wrong_size():
    """Test POST /api/icons/{tid} with wrong size (should return 400)"""
    try:
        tid = "0004000000164801"
        icon_data = generate_icon_data(4000)  # Wrong size
        
        response = requests.post(f"{API_BASE}/icons/{tid}",
                                data=icon_data,
                                headers={"Content-Type": "application/octet-stream"},
                                timeout=10)
        
        if response.status_code == 400:
            log_test("POST /api/icons/{tid} with wrong size returns 400", True,
                    f"Error: {response.json().get('detail', 'N/A')}")
        else:
            log_test("POST /api/icons/{tid} with wrong size returns 400", False,
                    f"Expected 400, got {response.status_code}")
    except Exception as e:
        log_error("POST /api/icons/{tid} with wrong size", str(e))

def test_icon_upload_invalid_tid_length():
    """Test POST /api/icons/{tid} with 15-char TID (should return 400)"""
    try:
        tid = "000400000016480"  # 15 chars instead of 16
        icon_data = generate_icon_data(4608)
        
        response = requests.post(f"{API_BASE}/icons/{tid}",
                                data=icon_data,
                                headers={"Content-Type": "application/octet-stream"},
                                timeout=10)
        
        if response.status_code == 400:
            log_test("POST /api/icons/{tid} with 15-char TID returns 400", True,
                    f"Error: {response.json().get('detail', 'N/A')}")
        else:
            log_test("POST /api/icons/{tid} with 15-char TID returns 400", False,
                    f"Expected 400, got {response.status_code}")
    except Exception as e:
        log_error("POST /api/icons/{tid} with 15-char TID", str(e))

def test_icon_upload_invalid_tid_chars():
    """Test POST /api/icons/{tid} with non-hex chars (should return 400)"""
    try:
        tid = "000400000016480G"  # 'G' is not hex
        icon_data = generate_icon_data(4608)
        
        response = requests.post(f"{API_BASE}/icons/{tid}",
                                data=icon_data,
                                headers={"Content-Type": "application/octet-stream"},
                                timeout=10)
        
        if response.status_code == 400:
            log_test("POST /api/icons/{tid} with non-hex chars returns 400", True,
                    f"Error: {response.json().get('detail', 'N/A')}")
        else:
            log_test("POST /api/icons/{tid} with non-hex chars returns 400", False,
                    f"Expected 400, got {response.status_code}")
    except Exception as e:
        log_error("POST /api/icons/{tid} with non-hex chars", str(e))

def test_icon_upload_lowercase_tid():
    """Test POST /api/icons/{tid} with lowercase TID (should accept and normalize)"""
    try:
        tid_lower = "0004000000164802"
        tid_upper = tid_lower.upper()
        icon_data = generate_icon_data(4608)
        
        response = requests.post(f"{API_BASE}/icons/{tid_lower}",
                                data=icon_data,
                                headers={"Content-Type": "application/octet-stream"},
                                timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            if data.get("tid") == tid_upper:
                log_test("POST /api/icons/{tid} with lowercase TID normalizes to uppercase", True,
                        f"Input: {tid_lower}, Stored: {data['tid']}")
            else:
                log_test("POST /api/icons/{tid} with lowercase TID normalizes to uppercase", False,
                        f"Expected {tid_upper}, got {data.get('tid')}")
        else:
            log_test("POST /api/icons/{tid} with lowercase TID normalizes to uppercase", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("POST /api/icons/{tid} with lowercase TID", str(e))

def test_icon_upload_idempotency():
    """Test POST /api/icons/{tid} twice (idempotency)"""
    try:
        tid = "0004000000164803"
        icon_data = generate_icon_data(4608)
        
        # First upload
        response1 = requests.post(f"{API_BASE}/icons/{tid}",
                                 data=icon_data,
                                 headers={"Content-Type": "application/octet-stream"},
                                 timeout=10)
        
        # Second upload (same data)
        response2 = requests.post(f"{API_BASE}/icons/{tid}",
                                 data=icon_data,
                                 headers={"Content-Type": "application/octet-stream"},
                                 timeout=10)
        
        if response1.status_code == 200 and response2.status_code == 200:
            log_test("POST /api/icons/{tid} idempotency (upload twice)", True,
                    "Both uploads returned 200")
        else:
            log_test("POST /api/icons/{tid} idempotency (upload twice)", False,
                    f"First: {response1.status_code}, Second: {response2.status_code}")
    except Exception as e:
        log_error("POST /api/icons/{tid} idempotency", str(e))

def test_get_icon_exists(tid: str):
    """Test GET /api/icons/{tid}.png for uploaded icon"""
    try:
        response = requests.get(f"{API_BASE}/icons/{tid}.png", timeout=10)
        
        if response.status_code == 200:
            content_type = response.headers.get("Content-Type", "")
            if "image/png" in content_type:
                log_test("GET /api/icons/{tid}.png returns uploaded icon", True,
                        f"Content-Type: {content_type}, Size: {len(response.content)} bytes")
            else:
                log_test("GET /api/icons/{tid}.png returns uploaded icon", False,
                        f"Wrong Content-Type: {content_type}")
        else:
            log_test("GET /api/icons/{tid}.png returns uploaded icon", False,
                    f"Status: {response.status_code}")
    except Exception as e:
        log_error("GET /api/icons/{tid}.png for uploaded icon", str(e))

def test_get_icon_not_exists():
    """Test GET /api/icons/{tid}.png for non-existent icon (should return 404)"""
    try:
        tid = "0004000000999999"  # Non-existent TID
        response = requests.get(f"{API_BASE}/icons/{tid}.png", timeout=10)
        
        if response.status_code == 404:
            log_test("GET /api/icons/{tid}.png for non-existent icon returns 404", True)
        else:
            log_test("GET /api/icons/{tid}.png for non-existent icon returns 404", False,
                    f"Expected 404, got {response.status_code}")
    except Exception as e:
        log_error("GET /api/icons/{tid}.png for non-existent icon", str(e))

def test_get_title_after_icon_upload(tid: str):
    """Test GET /api/titles/{tid} after icon upload (should have icon_url)"""
    try:
        response = requests.get(f"{API_BASE}/titles/{tid}", timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            if data.get("tid") == tid and data.get("icon_url") == f"/api/icons/{tid}.png":
                log_test("GET /api/titles/{tid} after icon upload has icon_url", True,
                        f"icon_url: {data['icon_url']}")
            else:
                log_test("GET /api/titles/{tid} after icon upload has icon_url", False,
                        f"Expected icon_url set, got: {data}")
        else:
            log_test("GET /api/titles/{tid} after icon upload has icon_url", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("GET /api/titles/{tid} after icon upload", str(e))

def test_get_title_not_exists():
    """Test GET /api/titles/{tid} for non-existent title (should return 200 with nulls)"""
    try:
        tid = "0004000000888888"  # Non-existent TID
        response = requests.get(f"{API_BASE}/titles/{tid}", timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            if (data.get("tid") == tid and 
                data.get("name") is None and 
                data.get("icon_url") is None and
                data.get("source") is None):
                log_test("GET /api/titles/{tid} for non-existent title returns 200 with nulls", True)
            else:
                log_test("GET /api/titles/{tid} for non-existent title returns 200 with nulls", False,
                        f"Expected nulls, got: {data}")
        else:
            log_test("GET /api/titles/{tid} for non-existent title returns 200 with nulls", False,
                    f"Expected 200, got {response.status_code}")
    except Exception as e:
        log_error("GET /api/titles/{tid} for non-existent title", str(e))

def test_post_title():
    """Test POST /api/titles/{tid} with name and source"""
    try:
        tid = "0004000000164804"
        payload = {
            "name": "Test Game Title",
            "source": "manual"
        }
        
        response = requests.post(f"{API_BASE}/titles/{tid}",
                                json=payload,
                                timeout=10)
        
        if response.status_code == 200:
            data = response.json()
            if (data.get("tid") == tid and 
                data.get("name") == payload["name"] and
                data.get("source") == payload["source"] and
                "updated_at" in data):
                log_test("POST /api/titles/{tid} upserts title", True,
                        f"Name: {data['name']}, Source: {data['source']}")
            else:
                log_test("POST /api/titles/{tid} upserts title", False,
                        f"Response missing fields or wrong values: {data}")
        else:
            log_test("POST /api/titles/{tid} upserts title", False,
                    f"Status: {response.status_code}, Body: {response.text}")
    except Exception as e:
        log_error("POST /api/titles/{tid}", str(e))

def print_summary():
    """Print test summary"""
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    print(f"✅ Passed: {len(test_results['passed'])}")
    print(f"❌ Failed: {len(test_results['failed'])}")
    print(f"⚠️  Errors: {len(test_results['errors'])}")
    
    if test_results['failed']:
        print("\nFailed Tests:")
        for fail in test_results['failed']:
            print(f"  - {fail['name']}")
            if fail['details']:
                print(f"    {fail['details']}")
    
    if test_results['errors']:
        print("\nTest Errors:")
        for err in test_results['errors']:
            print(f"  - {err['name']}")
            print(f"    {err['error']}")
    
    print("="*70)
    
    # Return exit code
    return 0 if not test_results['failed'] and not test_results['errors'] else 1

def main():
    """Run all tests"""
    print("="*70)
    print("TriCord Presence Backend API Test Suite")
    print(f"Base URL: {BASE_URL}")
    print("="*70)
    print()
    
    # Test existing endpoints
    print("Testing Existing Endpoints:")
    print("-" * 70)
    test_root_endpoint()
    test_installer_info()
    test_download_cia()
    test_status_endpoints()
    print()
    
    # Test new Phase C endpoints
    print("Testing Phase C Endpoints (Icons/Titles):")
    print("-" * 70)
    
    # Icon upload tests
    uploaded_tid = test_icon_upload_valid()
    test_icon_upload_wrong_size()
    test_icon_upload_invalid_tid_length()
    test_icon_upload_invalid_tid_chars()
    test_icon_upload_lowercase_tid()
    test_icon_upload_idempotency()
    
    # Icon retrieval tests
    if uploaded_tid:
        test_get_icon_exists(uploaded_tid)
        test_get_title_after_icon_upload(uploaded_tid)
    test_get_icon_not_exists()
    
    # Title tests
    test_get_title_not_exists()
    test_post_title()
    print()
    
    # Print summary and exit
    exit_code = print_summary()
    sys.exit(exit_code)

if __name__ == "__main__":
    main()
