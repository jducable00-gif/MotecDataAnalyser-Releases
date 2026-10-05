#!/usr/bin/env python3
import json
from pathlib import Path

def extract_channels_from_json(json_path, output_md_path):
    """
    Extrait les canaux et leurs unités depuis le fichier JSON des fichiers LD 
    et génère un fichier MD avec le format souhaité.
    """
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    # Filtrer les entrées du dossier AC_EVO
    ac_evo_channels = []
    for entry in data:
        if entry.get("folder") == "AC_EVO" and "channels" in entry:
            for channel in entry["channels"]:
                channel_name = channel.get("name", "")
                unit = channel.get("unit", "")
                if channel_name:  # Ignorer les canaux sans nom
                    ac_evo_channels.append({"name": channel_name, "unit": unit})
    
    # Écrire le fichier MD
    with open(output_md_path, 'w', encoding='utf-8') as f:
        for i, channel in enumerate(ac_evo_channels, 1):
            f.write(f"{i}. **{channel['name']}** - {channel['unit']}\n")

if __name__ == "__main__":
    json_path = Path("c:/Users/Raymond/Documents/MotecDataAnalyser/data/_channel_extract.json")
    output_md_path = Path("c:/Users/Raymond/Documents/MotecDataAnalyser/AC_EVO_channels.md")
    extract_channels_from_json(json_path, output_md_path)
    print(f"Fichier MD généré avec succès: {output_md_path}")