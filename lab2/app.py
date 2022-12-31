from flask import Flask, url_for, redirect, render_template, request, jsonify, Response
import os
import time

app = Flask(__name__)


@app.route('/')
def index():
    return render_template("index.html")

 
if __name__ == '__main__':
    app.run(debug=True, port = 8080)