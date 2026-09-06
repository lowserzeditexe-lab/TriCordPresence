import requests
import hashlib
import sys

class CIAInstallerAPITester:
    def __init__(self, base_url="https://presence-installer.preview.emergentagent.com"):
        self.base_url = base_url
        self.tests_run = 0
        self.tests_passed = 0
        self.expected_sha256 = "456796fbd7df27800aed01e6ae10396e8a7cf8cd6e223bdb03ec3ae0b69da979"
        self.expected_size = 1688512
        self.expected_filename = "tricord-presence-installer.cia"

    def run_test(self, name, test_func):
        """Run a single test"""
        self.tests_run += 1
        print(f"\n🔍 Testing {name}...")
        
        try:
            success = test_func()
            if success:
                self.tests_passed += 1
                print(f"✅ Passed")
            else:
                print(f"❌ Failed")
            return success
        except Exception as e:
            print(f"❌ Failed - Error: {str(e)}")
            return False

    def test_installer_info(self):
        """Test GET /api/installer/info endpoint"""
        url = f"{self.base_url}/api/installer/info"
        
        try:
            response = requests.get(url, timeout=10)
            
            if response.status_code != 200:
                print(f"   Expected status 200, got {response.status_code}")
                return False
            
            data = response.json()
            
            # Check all required fields
            required_fields = ["filename", "title", "version", "size", "sha256", "download_path"]
            for field in required_fields:
                if field not in data:
                    print(f"   Missing field: {field}")
                    return False
            
            # Validate specific values
            if data["filename"] != self.expected_filename:
                print(f"   Expected filename '{self.expected_filename}', got '{data['filename']}'")
                return False
            
            if data["version"] != "2":
                print(f"   Expected version '2', got '{data['version']}'")
                return False
            
            if data["size"] != self.expected_size:
                print(f"   Expected size {self.expected_size}, got {data['size']}")
                return False
            
            if data["sha256"] != self.expected_sha256:
                print(f"   Expected SHA-256 {self.expected_sha256}, got {data['sha256']}")
                return False
            
            if data["download_path"] != f"/api/download/{self.expected_filename}":
                print(f"   Expected download_path '/api/download/{self.expected_filename}', got '{data['download_path']}'")
                return False
            
            if len(data["sha256"]) != 64:
                print(f"   SHA-256 should be 64 hex chars, got {len(data['sha256'])}")
                return False
            
            print(f"   ✓ filename: {data['filename']}")
            print(f"   ✓ title: {data['title']}")
            print(f"   ✓ version: {data['version']}")
            print(f"   ✓ size: {data['size']} bytes")
            print(f"   ✓ sha256: {data['sha256'][:16]}...{data['sha256'][-16:]}")
            print(f"   ✓ download_path: {data['download_path']}")
            
            return True
            
        except Exception as e:
            print(f"   Exception: {str(e)}")
            return False

    def test_download_cia_get(self):
        """Test GET /api/download/tricord-presence-installer.cia endpoint"""
        url = f"{self.base_url}/api/download/{self.expected_filename}"
        
        try:
            response = requests.get(url, timeout=30, stream=True)
            
            if response.status_code != 200:
                print(f"   Expected status 200, got {response.status_code}")
                return False
            
            # Check Content-Type
            content_type = response.headers.get('Content-Type', '')
            if content_type != 'application/octet-stream':
                print(f"   Expected Content-Type 'application/octet-stream', got '{content_type}'")
                return False
            
            # Check Content-Length
            content_length = response.headers.get('Content-Length')
            if content_length:
                content_length = int(content_length)
                if content_length != self.expected_size:
                    print(f"   Expected Content-Length {self.expected_size}, got {content_length}")
                    return False
            
            # Download and verify content
            h = hashlib.sha256()
            size = 0
            for chunk in response.iter_content(chunk_size=1024 * 1024):
                h.update(chunk)
                size += len(chunk)
            
            if size != self.expected_size:
                print(f"   Expected body size {self.expected_size} bytes, got {size} bytes")
                return False
            
            sha256 = h.hexdigest()
            if sha256 != self.expected_sha256:
                print(f"   Expected SHA-256 {self.expected_sha256}, got {sha256}")
                return False
            
            print(f"   ✓ Status: 200")
            print(f"   ✓ Content-Type: {content_type}")
            print(f"   ✓ Content-Length: {content_length}")
            print(f"   ✓ Body size: {size} bytes")
            print(f"   ✓ SHA-256: {sha256[:16]}...{sha256[-16:]}")
            
            return True
            
        except Exception as e:
            print(f"   Exception: {str(e)}")
            return False

    def test_download_cia_head(self):
        """Test HEAD /api/download/tricord-presence-installer.cia endpoint"""
        url = f"{self.base_url}/api/download/{self.expected_filename}"
        
        try:
            response = requests.head(url, timeout=10)
            
            if response.status_code != 200:
                print(f"   Expected status 200, got {response.status_code}")
                return False
            
            # Check Content-Length
            content_length = response.headers.get('Content-Length')
            if not content_length:
                print(f"   Missing Content-Length header")
                return False
            
            content_length = int(content_length)
            if content_length != self.expected_size:
                print(f"   Expected Content-Length {self.expected_size}, got {content_length}")
                return False
            
            print(f"   ✓ Status: 200")
            print(f"   ✓ Content-Length: {content_length}")
            
            return True
            
        except Exception as e:
            print(f"   Exception: {str(e)}")
            return False

    def test_download_unknown_404(self):
        """Test GET /api/download/unknown.cia returns 404"""
        url = f"{self.base_url}/api/download/unknown.cia"
        
        try:
            response = requests.get(url, timeout=10)
            
            if response.status_code != 404:
                print(f"   Expected status 404, got {response.status_code}")
                return False
            
            print(f"   ✓ Status: 404 (as expected)")
            
            return True
            
        except Exception as e:
            print(f"   Exception: {str(e)}")
            return False

def main():
    print("=" * 60)
    print("TriCord Presence Installer - Backend API Tests")
    print("=" * 60)
    
    tester = CIAInstallerAPITester()
    
    # Run all tests
    tester.run_test("GET /api/installer/info", tester.test_installer_info)
    tester.run_test("GET /api/download/tricord-presence-installer.cia", tester.test_download_cia_get)
    tester.run_test("HEAD /api/download/tricord-presence-installer.cia", tester.test_download_cia_head)
    tester.run_test("GET /api/download/unknown.cia (404)", tester.test_download_unknown_404)
    
    # Print results
    print("\n" + "=" * 60)
    print(f"📊 Tests passed: {tester.tests_passed}/{tester.tests_run}")
    print("=" * 60)
    
    return 0 if tester.tests_passed == tester.tests_run else 1

if __name__ == "__main__":
    sys.exit(main())
