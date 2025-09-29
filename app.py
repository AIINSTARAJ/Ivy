from flask import *

from sqlalchemy import desc

from App.model import *

from AI.agent import *

app = Flask(__name__,static_folder='UI',template_folder='UI')

path = os.path.join(os.path.abspath(os.path.dirname(__file__)), "data", "data.db")
app.config['SQLALCHEMY_DATABASE_URI'] = f"sqlite:///{path}"
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

db.init_app(app)

@app.route('/',methods = ['GET'])
def index():
    data = Data.query.order_by(Data.id.desc()).first()
    Items = Data.query.order_by(Data.id.desc()).limit(20).all()

    return render_template('index.html', data = data, Data = Items)

@app.route('/data',methods = ['GET','POST'])
def data():
    data = request.get_json()

    temp = data['Temp']
    humid = data['Humid']
    proxy = data['Proxy']

    response = report(temp, humid, proxy)
    parsed_resp = json.loads(response)

    text = parsed_resp["text"].strip()
    recommendation = parsed_resp["recommendation"].strip()
    confidence = parsed_resp["confidence"]

    data_ = Data(
        Temp=int(temp), 
        Humid=int(humid), 
        Proxy=int(proxy),
        Resp=text,
        Sugg=recommendation,
        Confid=confidence
    )

    db.session.add(data_)
    db.session.commit()

    return jsonify({
        "status": "ok",
        "id": data_.id,
        "Display": text,
        "Sugg": recommendation,
        "Confid": confidence
    }), 200


if __name__ == '__main__':
    with app.app_context():
        db.create_all()
    port = int(os.environ.get("PORT", 5005))
    app.run(debug=True,host='0.0.0.0',port=port)
