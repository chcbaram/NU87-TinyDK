#!/usr/bin/env python3
"""같은 네트워크의 NU87 보드를 찾는다.

브로드캐스트로 "NU87?" 를 던지고 응답한 보드를 모아 보여준다.
펌웨어 쪽은 ap/modules/network/net/net.c 의 netDiscoverPoll() 이다.

    python3 discover.py
    python3 discover.py --timeout 3
"""

import argparse
import socket
import sys

PORT = 50000
REQ = b"NU87?"


def broadcast_addrs():
    """브로드캐스트로 보낼 주소들.

    255.255.255.255 는 인터페이스가 여러 개일 때 하나로만 나가는 OS 가 있어서
    붙어 있는 각 서브넷의 브로드캐스트 주소도 같이 시도한다."""
    addrs = ["255.255.255.255"]

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip.startswith("127."):
                continue
            addrs.append(ip.rsplit(".", 1)[0] + ".255")
    except socket.gaierror:
        pass

    return list(dict.fromkeys(addrs))


def discover(timeout):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)

    for addr in broadcast_addrs():
        try:
            sock.sendto(REQ, (addr, PORT))
        except OSError:
            pass

    found = {}
    while True:
        try:
            data, src = sock.recvfrom(256)
        except socket.timeout:
            break

        field = data.decode("utf-8", "replace").split()
        if len(field) < 5 or field[0] != "NU87":
            continue

        found[src[0]] = {"name": field[1], "ver": field[2], "mac": field[3]}

    sock.close()
    return found


def main():
    parser = argparse.ArgumentParser(description="NU87 보드 찾기")
    parser.add_argument("--timeout", type=float, default=2.0, help="응답 대기 초 (기본 2)")
    args = parser.parse_args()

    found = discover(args.timeout)
    if not found:
        print("찾은 보드가 없다")
        return 1

    print(f"\n{'ip':<16} {'이름':<14} {'버전':<12} mac")
    print("-" * 62)
    for ip, info in sorted(found.items()):
        print(f"{ip:<16} {info['name']:<14} {info['ver']:<12} {info['mac']}")
    print(f"\n{len(found)} 대\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
