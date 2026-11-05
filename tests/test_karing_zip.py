import io
import json
import os
import subprocess
import tempfile
import unittest
import warnings
import zipfile


class BackupArchiveTests(unittest.TestCase):
    def run_import(self, data, succeeds):
        with tempfile.NamedTemporaryFile() as file:
            file.write(data)
            file.flush()
            subprocess.run(["./test_karing_backup", file.name, str(int(succeeds))], check=True)

    def archive(self, entries, compression=zipfile.ZIP_DEFLATED):
        output = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(output, "w", compression=compression) as archive:
                for name, data in entries:
                    archive.writestr(name, data)
        return output.getvalue()

    def test_stored_and_deflated(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [
            {"type": "socks", "tag": "proxy", "server": "proxy.example", "server_port": 1080}
        ]}]})
        for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            archive = self.archive([("karing_subscribe.json", payload)], compression)
            self.run_import(archive, True)
            self.run_import(archive[:-1], False)
            damaged = bytearray(archive)
            damaged[50] ^= 1
            self.run_import(damaged, False)

    def test_socks5_alias(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [
            {"type": "socks5", "server": "proxy.example", "server_port": 1080}
        ]}]})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), True)

    def test_export_opens_with_standard_zip_reader(self):
        with tempfile.NamedTemporaryFile() as file:
            subprocess.run(["./test_karing_backup", "--export", file.name], check=True)
            with zipfile.ZipFile(file.name) as archive:
                self.assertIsNone(archive.testzip())
                payload = json.loads(archive.read("karing_subscribe.json"))
        groups = payload["items"]
        self.assertEqual(groups[0]["groupid"], "custom")
        self.assertEqual(groups[1]["urlOrPath"], "https://feed.example/sub")
        self.assertEqual(groups[1]["servers"][0]["server"], "proxy.example")
        self.assertEqual(groups[1]["servers"][0]["password"], "pass")

    def test_data_descriptor(self):
        class NonSeekable(io.BytesIO):
            def seekable(self):
                return False
            def seek(self, *args):
                raise io.UnsupportedOperation("seek")

        payload = json.dumps({"items": [{"groupid": "custom", "servers": [
            {"type": "socks", "server": "proxy.example", "server_port": 1080}
        ]}]})
        output = NonSeekable()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("karing_subscribe.json", payload)
        self.run_import(output.getvalue(), True)

    def test_missing_and_duplicate_entry(self):
        payload = '{"items":[]}'
        self.run_import(self.archive([("other.json", payload)]), False)
        self.run_import(self.archive([("karing_subscribe.json", payload),
                                      ("karing_subscribe.json", payload)]), False)

    def test_certificate_pinning_is_not_discarded(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [{
            "type": "vless", "server": "proxy.example", "server_port": 443,
            "uuid": "11111111-1111-4111-8111-111111111111",
            "tls": {"enabled": True, "certificate_public_key_sha256": ["pin"]}
        }]}]})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_overlong_tls_name_is_not_discarded(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [{
            "type": "vless", "server": "proxy.example", "server_port": 443,
            "uuid": "11111111-1111-4111-8111-111111111111",
            "tls": {"enabled": True, "server_name": "a" * 300}
        }]}]})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_http_proxy_tls_name_is_not_discarded(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [{
            "type": "http", "server": "proxy.example", "server_port": 443,
            "tls": {"enabled": True, "server_name": "other.example"}
        }]}]})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_subscription_decryption_is_not_discarded(self):
        payload = json.dumps({"items": [{"groupid": "feed", "urlOrPath": "https://feed.example/",
                                         "decryptPasswordAES": "secret", "servers": []}]})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_excessive_json_entries_are_rejected(self):
        payload = json.dumps({"items": [{"groupid": "custom", "servers": [
            {"type": "socks", "server": "proxy.example", "server_port": 1080}
        ]}], "extra": [0] * 131073})
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_deep_json_is_rejected(self):
        payload = '{"items":[],"extra":' + '[' * 65 + '0' + ']' * 65 + '}'
        self.run_import(self.archive([("karing_subscribe.json", payload)]), False)

    def test_invalid_json(self):
        self.run_import(self.archive([("karing_subscribe.json", '{"items":{}}')]), False)


if __name__ == "__main__":
    unittest.main()
