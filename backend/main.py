from flask import Flask, request, jsonify

app = Flask(__name__)


@app.route("/")
def hello_world():
    return jsonify({"message":"Flask for ESP32"}), 200



@app.route("/fetch", methods=["POST"])
def fetch():

    if request.is_json:
        data = request.get_json()
        print("DATA")
        print(data)
        return jsonify({"mesaj": "Veri başarıyla alındı!"}), 200
    
    else:
        return jsonify({"hata": "Veri JSON formatında değil!"}), 400


@app.route("/send", methods=["POST"])
def send():

    return jsonify({
        "mesaj": True
        }), 200


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)