import socket
import json

def send_request(host, port, request):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.connect((host, port))
        sock.sendall((json.dumps(request) + '\n').encode())
        response = sock.recv(4096).decode()
        return json.loads(response)

print("Регистрация клиента")
reg_req = {
    "action": "register",
    "data": {
        "name": "Анна Петрова",
        "email": "anna@mail.ru",
        "password": "pass123",
        "role": "client"
    }
}
resp = send_request('localhost', 12345, reg_req)
print(resp)

print("\nЛогин")
login_req = {
    "action": "login",
    "data": {
        "email": "anna@mail.ru",
        "password": "pass123"
    }
}
resp = send_request('localhost', 12345, login_req)
print(resp)

print("\nСписок психологов")
list_req = {"action": "get_psychologists", "data": {}}
resp = send_request('localhost', 12345, list_req)
print(resp)
#python C:\QtProject\PsychoServer\tests\test_client.py