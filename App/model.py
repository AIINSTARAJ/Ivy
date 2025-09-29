import sys 

sys.path.insert(0,'../../')

from sqlalchemy.sql import * 

from werkzeug.security import *

from flask_sqlalchemy import SQLAlchemy

db = SQLAlchemy()

class Data(db.Model):
    __tablename__ = 'data'
    id = db.Column(db.Integer,primary_key = True)
    Temp = db.Column(db.Integer, nullable = False)
    Humid = db.Column(db.Integer, nullable = False)
    Proxy = db.Column(db.Integer, nullable = False)
    Resp = db.Column(db.String(500),nullable=False)
    Sugg = db.Column(db.String(500),nullable=False)
    Confid = db.Column(db.Integer, nullable = False)
    Time = db.Column(db.DateTime(timezone=True), server_default = func.now())

    def __repr__(self):
        return f'<Data {self.id}>'