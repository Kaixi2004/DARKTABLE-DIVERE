================================================================================
Camera Support
================================================================================

.. exec::
    from xml.dom.minidom import parse
    import xml.dom.minidom
    import sys
    import csv

    DOMTree = xml.dom.minidom.parse("data/cameras.xml")
    cameras = DOMTree.documentElement.getElementsByTagName("Camera")

    unique_makes = dict()

    for camera in cameras:
        make = ''
        model = ''

        if len(camera.getElementsByTagName('ID')) > 0:
            ID = camera.getElementsByTagName('ID')[0]
            make = ID.getAttribute("make")
            model = ID.getAttribute("model")
        else:
            make = camera.getAttribute("make")
            model = camera.getAttribute("model")

        unique_makes[make] = unique_makes.get(make, dict())
        unique_makes[make][model] = unique_makes[make].get(model, dict())
        unique_makes[make][model]['modes'] = unique_makes[
            make][model].get('modes', set())
        unique_makes[make][model]['aliases'] = unique_makes[
            make][model].get('aliases', set())

        mode = camera.getAttribute("mode")
        if mode == '':
            mode = 'Default mode'
        if camera.hasAttribute("supported"):
            if camera.getAttribute("supported") == 'no':
                mode += " - *unsupported*"
            if 'unknown' in camera.getAttribute("supported"):
                mode += " - *unknown*"

        unique_makes[make][model]['modes'].add(mode)

        for alias in camera.getElementsByTagName('Alias'):
            if alias.getAttribute("id") != '':
                unique_makes[make][model][
                    'aliases'].add(alias.getAttribute("id"))
            else:
                unique_makes[make][model][
                    'aliases'].add(alias.childNodes[0].data)

    from collections import OrderedDict

    unique_makes = OrderedDict(sorted(unique_makes.items()))

    print("There are %i known camera makers, " % len(unique_makes))

    count_unique_makesmodels = 0
    for make, models in unique_makes.items():
        count_unique_makesmodels += len(models)

    print("%i known unique camera models.\n" % count_unique_makesmodels)

    print(
        "Any support is impossible without the samples.\nCurrently, |rpu-button-cameras| cameras have samples, with total count of |rpu-button-samples| unique samples. **Please contribute samples**!\n\n")

    print(".. csv-table:: Supported cameras")
    print("   :header-rows: 1")

    print("")

    sys.stdout.flush()

    csvwriter = csv.writer(sys.stdout, delimiter=',',
                           quotechar='\"', quoting=csv.QUOTE_ALL)

    # Header
    sys.stdout.write("   ")
    csvwriter.writerow(['Maker', 'Camera', 'Supported', 'Aliases', 'Modes'])

    # Rows
    for make, models in unique_makes.items():
        models = OrderedDict(sorted(models.items()))
        for model, content in models.items():
            # Leaf cameras have aliases in their model name, we need to split them here
            aliases = model.split("/", 1) if make == "Leaf" else [model]
            model_name = aliases[0]

            # Concatenate unique official aliases with the ones we found above
            aliases = [a for a in list(content['aliases']) if a != model_name] + aliases[1:]

            modes = [mode for mode in content['modes']
                    if "unsupported" not in mode]

            if len(modes) == 0:
                supported = "✗"
            else:
                modes = [mode for mode in modes
                        if "unknown" not in mode]

                if len(modes) == 0:
                    supported = "?"
                else:
                    supported = "✓"

            sys.stdout.write("   ")
            csvwriter.writerow([make, model_name, supported, ', '.join(aliases), ', '.join(content['modes'])])


.. |rpu-button-cameras| image:: https://raw.pixls.us/button-cameras.svg
    :target: https://raw.pixls.us/

.. |rpu-button-samples| image:: https://raw.pixls.us/button-samples.svg
    :target: https://raw.pixls.us/
