import json
import os

from dotenv import load_dotenv
from langchain_google_genai import GoogleGenerativeAI
from langchain.prompts import PromptTemplate

load_dotenv()

google_api_key = os.environ.get('GOOGLE_API_KEY')

llm = GoogleGenerativeAI(model="gemini-2.0-flash", temperature=0.5, google_api_key=google_api_key)

PROMPT_TEMPLATE = """
    You are an advanced environmental intelligence agent: an expert storyteller and analyst who
    turns raw sensor inputs into a short, vivid, and useful human-friendly micro-report.

    INPUTS
    -------
    Temperature (°C): {Temp}
    Humidity (%): {Humid}
    Distance/Proxy (meters): {Proxy}

    INSTRUCTIONS TO THE AGENT
    -------------------------
        1. Perform your internal reasoning (do not output any chain-of-thought or stepwise reasoning).
        2. Produce a concise, high-creativity textual micro-report in natural language (no more than 2(if possible, just a single) short sentences).
            - The text should avoid formulas and long technical paragraphs.
            - It should sound fresh and human (a little poetic allowed), and you may include 1-2 emojis.
        3. Then provide one short, actionable recommendation.
        4. At the end provide a numeric confidence score (0-100) representing how certain you are about the interpretation.
        5. **Output MUST be valid JSON only** with exactly these keys:
            - "text": string (the creative micro-report)
            - "recommendation": string (short actionable sentence)
            - "confidence": integer (0-100)
        6. Do NOT include any extra commentary, delimiters, or markdown — only the JSON.

    CONTEXT / TONE
    --------------
    - Make the report vivid, helpful, and professional.
    - Keep length short: aim for a total of around 1-3 sentences in "text".
    - Use emojis sparingly to convey tone (e.g., 🌡️, 💧, 🧭, ⚠️).

    EXAMPLE (not actual output; for style only and format)
    {"text": "Warm breeze rolling in, humidity low enough for crisp air — sensors show a mild thermal spike. 🌡️", "recommendation": "If outdoors, hydrate and avoid direct sun for 30 minutes.", "confidence": 86}
    {"text": "The air feels heavy with warmth and dampness, a humid cloak settling around everything 🌡️💧", "recommendation": "Keep hydrated and avoid extended outdoor activity in direct sunlight. ", "confidence":"90"}

    Now produce JSON for the given inputs.
"""

prompt = PromptTemplate(
    input_variables=["Temp", "Humid", "Proxy"],
    template=PROMPT_TEMPLATE
)


def report(temp: float, humid: float, proxy: float) -> str:
    """
    Generate a short creative micro-report from Temp (°C), Humid (%), and Proxy (meters).
    Returns the 'text' field from the model's JSON output.
    """
    temp = round(float(temp), 2)
    humid = round(float(humid), 2)
    proxy = round(float(proxy), 2)

    prompt_ = prompt.format(Temp=temp, Humid=humid, Proxy=proxy)

    try:
        resp = llm.invoke(prompt_)

        try:
            json.loads(resp)
            return resp

        except json.JSONDecodeError:
            return resp

    except Exception as e:
        return f"Error."
